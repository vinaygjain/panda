/* PANDABEGINCOMMENT
* 
* Authors:
*  Vinay Jain             vganeshmalja@cs.stonybrook.edu
* 
* This work is licensed under the terms of the GNU GPL, version 2. 
* See the COPYING file in the top-level directory. 
* 
PANDAENDCOMMENT */
// This needs to be defined before anything is included in order to get
// the PRIx64 macro
#define __STDC_FORMAT_MACROS

extern "C" {
#include "config.h"
#include "qemu-common.h"
#include "monitor.h"
#include "cpu.h"
#include "disas.h"
#include "time.h"

#include "panda_plugin.h"
#include <stdio.h>
#include <stdlib.h>
}

#include <ctype.h>
#include <cassert>
#include <functional>
#include <string>
#include <math.h>
#include <map>
#include <list>
#include <algorithm>

#define DEFAULT_LOG_FILE "nsac_task_struct.txt"
#define MAX_RECORDS	1000
#define INTERVAL	2
#define MAX_PROCESSES		2000

/*
	To get the TASK_INIT run the following: "sudo grep init_task /boot/Symbol.map-*"
	nly consider the 7 LSBs
	he offsets provided here are kernel specific.
*/
#define TASK_INIT		0x160d020
#define TASK_0_PID_OFFSET	481
#define TASK_0_COMM_OFFSET	920
#define TASK_0_TASKS_OFFSET	368
#define TASK_PID_OFFSET		(481-368)
#define TASK_COMM_OFFSET	(920-368)
#define TASK_TASKS_OFFSET	(368-368)
#define HEX3			(256*256*256)
#define HEX2			(256*256)
#define HEX1			256

// These need to be extern "C" so that the ABI is compatible with
// QEMU/PANDA, which is written in C
extern "C" {
	bool init_plugin(void *);
	void uninit_plugin(void *);
	int mem_write_callback(CPUState *env, target_ulong pc, target_ulong addr,
	target_ulong size, void *buf);
}

// Timestamps
uint64_t curr_stamp;
struct timeval tv;
uint64_t start_seconds;
uint64_t last_record_time;

void UpdateTimeStamp(void) {
	gettimeofday(&tv,NULL);
	curr_stamp = (((uint64_t)tv.tv_sec)-start_seconds)*(uint64_t)1000000+tv.tv_usec;
}

// This is where we'll write out the memstat data
FILE *plugin_log;
int enabled;

uint64_t bytes_read, bytes_written;
uint64_t num_reads, num_writes;

// Recording

struct record {
	int count;
	int pid[MAX_PROCESSES];
	target_ulong addr[MAX_PROCESSES];
	target_ulong next_ts[MAX_PROCESSES];
	char name[MAX_PROCESSES][16];
};

int record_count;
struct record record_A, record_B;
struct record *record_curr;
struct record *record_old;

void print_task_struct(void) {

#ifdef CONFIG_SOFTMMU
	int res = -1, i, j;
	uint8_t local_buf[TARGET_PAGE_SIZE];
	ram_addr_t addr = TASK_INIT;

	if (record_count%2 == 0) {
		record_curr = &record_A;
		record_old = &record_B;
	} else {
		record_curr = &record_B;
		record_old = &record_A;
	}

	if (addr < ram_size)
		res = panda_physical_memory_rw(addr, local_buf, TARGET_PAGE_SIZE, 0);

	if (res != -1) {

		target_ulong pid = ((((int)local_buf[TASK_0_PID_OFFSET]) * HEX3) + \
					(((int)local_buf[TASK_0_PID_OFFSET+1]) * HEX2) + \
					(((int)local_buf[TASK_0_PID_OFFSET+2]) * HEX1) + \
					((int)local_buf[TASK_0_PID_OFFSET+3]));

		target_ulong next_addr = ((local_buf[TASK_0_TASKS_OFFSET]) + \
						((local_buf[TASK_0_TASKS_OFFSET+1]) * HEX1) + \
						((local_buf[TASK_0_TASKS_OFFSET+2]) * HEX2) + \
						((local_buf[TASK_0_TASKS_OFFSET+3]) * HEX3));

		record_curr->count = 0;
		record_curr->pid[record_curr->count] = (int)pid;
		record_curr->addr[record_curr->count] = (target_ulong)addr;
		record_curr->next_ts[record_curr->count] = (target_ulong)next_addr;
		strncpy(record_curr->name[record_curr->count], (char *)&local_buf[TASK_0_COMM_OFFSET], 16);
		record_curr->count = record_curr->count + 1;

		addr = next_addr;
	}

	i = MAX_PROCESSES;
	while(--i) {
		if ((addr+TARGET_PAGE_SIZE) < ram_size)
			res = panda_physical_memory_rw(addr, local_buf, TARGET_PAGE_SIZE, 0);
		else
			break;

		if (res != -1) {

			target_ulong pid = ((((int)local_buf[TASK_PID_OFFSET]) * HEX3) + \
						(((int)local_buf[TASK_PID_OFFSET+1]) * HEX2) + \
						(((int)local_buf[TASK_PID_OFFSET+2]) * HEX1) + \
						((int)local_buf[TASK_PID_OFFSET+3]));

			if (pid <= 0)
				break;

			target_ulong next_addr = ((local_buf[TASK_TASKS_OFFSET]) + \
							((local_buf[TASK_TASKS_OFFSET+1]) * HEX1) + \
							((local_buf[TASK_TASKS_OFFSET+2]) * HEX2) + \
							((local_buf[TASK_TASKS_OFFSET+3]) * HEX3));

			record_curr->pid[record_curr->count] = (int)pid;
			record_curr->addr[record_curr->count] = (target_ulong)addr;
			record_curr->next_ts[record_curr->count] = (target_ulong)next_addr;
			strncpy(record_curr->name[record_curr->count], (char *)&local_buf[TASK_COMM_OFFSET], 16);
			record_curr->count = record_curr->count + 1;

			addr = next_addr;
		} else
			break;
	}

	if (record_count == 0) // Print to file.
		for (i = 0; i < record_curr->count; i++)
			fprintf(plugin_log, "Addr: " TARGET_FMT_lx " PID: %04d Name: %16s Next: " TARGET_FMT_lx "\n",
					record_curr->addr[i],
					record_curr->pid[i],
					record_curr->name[i],
					record_curr->next_ts[i]);
	else {
		int removed[record_old->count];
		int found;
		for (i = 0; i < record_old->count; i++)
			removed[i] = 1;

		// Added processes.
		for (i = 0; i < record_curr->count; i++) {
			found = 0;
			for (j = 0; j < record_old->count; j++) {
				if (record_curr->pid[i] == record_old->pid[j]) {
					removed[j] = 0;
					found = 1;
					//break;
				}
			}

			if (found == 0)
				fprintf(plugin_log, "Added:   Addr: " TARGET_FMT_lx " PID: %04d Name: %16s Next: " TARGET_FMT_lx "\n",
							record_curr->addr[i],
							record_curr->pid[i],
							record_curr->name[i],
							record_curr->next_ts[i]);
		}

		// Removed processes.
		for (i = 0; i < record_old->count; i++)
			if (removed[i] == 1)
				fprintf(plugin_log, "Removed: Addr: " TARGET_FMT_lx " PID: %04d Name: %16s Next: " TARGET_FMT_lx "\n",
							record_old->addr[i],
							record_old->pid[i],
							record_old->name[i],
							record_old->next_ts[i]);	
	}
#endif
}

int mem_write_callback(CPUState *env, target_ulong pc, target_ulong addr,
	target_ulong size, void *buf) {

	bytes_written += size;
	num_writes++;

	UpdateTimeStamp();

	if (record_count < MAX_RECORDS &&
		(curr_stamp - last_record_time) > (uint64_t)(1000000*INTERVAL)) {

		last_record_time = curr_stamp;
		fprintf(plugin_log, "\nTASK_STRUCT\tTime: %lu\n", curr_stamp);
		print_task_struct();
		record_count++;
	} 

	return 1;
}

panda_arg_list *args;

bool init_plugin(void *self) {

	printf("Initializing plugin nsac_memstats\n");
	int i;
	char *nmlog_filename = NULL;
	args = panda_get_args("nsac_memstats");

	if (args != NULL) {
		for (i = 0; i < args->nargs; i++) {
			// Format is nsac_memstats:file=<file>
			if (0 == strncmp(args->list[i].key, "file", 4)) {
				nmlog_filename = args->list[i].value;
			}
		}
	}

	if (!nmlog_filename) {
		fprintf(stderr, "warning: File not provided."
			" Using default: %s\n", DEFAULT_LOG_FILE);
		char *nmdef=new char[strlen(DEFAULT_LOG_FILE)+1];
		strcpy(nmdef,DEFAULT_LOG_FILE);
		nmlog_filename=nmdef;
	}

	plugin_log = fopen(nmlog_filename, "w");
	if (!plugin_log) {
		fprintf(stderr, "Couldn't open %s. Abort.\n", nmlog_filename);
		return false;
	}

	record_count = 0;

	// Setting the starting seconds and last_record_time.
	gettimeofday(&tv,NULL);
	start_seconds = (uint64_t)tv.tv_sec;
	UpdateTimeStamp(); 
	last_record_time = curr_stamp;

	panda_cb pcb;

	// Enable Precise PC
	panda_do_flush_tb();
	panda_enable_precise_pc();
	enabled = 1;

	// Enable memory logging
	panda_enable_memcb();

	pcb.virt_mem_write = mem_write_callback;
	panda_register_callback(self, PANDA_CB_VIRT_MEM_WRITE, pcb);

	return true;
}

void uninit_plugin(void *self) {

	if (enabled == 1) {
		// Disable Precise PC
		panda_do_flush_tb();
		panda_disable_precise_pc();
		enabled = 0;
	}

	fflush(plugin_log);
	fclose(plugin_log);

	printf("Memory statistics: %lu loads, %lu stores, "
		"%lu bytes read, %lu bytes written.\n",
		num_reads, num_writes, bytes_read, bytes_written);
}
