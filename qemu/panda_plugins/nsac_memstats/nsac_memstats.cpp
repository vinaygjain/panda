/* PANDABEGINCOMMENT
* 
* Authors:
*  Vinay Jain             vganeshmalja@stonybrook.edu
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

#define DEFAULT_LOG_FILE "nsac_memstats.txt"
#define MAX_RECORDS 100000000
// These need to be extern "C" so that the ABI is compatible with
// QEMU/PANDA, which is written in C
extern "C" {
	bool init_plugin(void *);
	void uninit_plugin(void *);
	int mem_write_callback(CPUState *env, target_ulong pc, target_ulong addr,
				target_ulong size, void *buf);
	int mem_read_callback(CPUState *env, target_ulong pc, target_ulong addr,
				target_ulong size, void *buf);
}

struct record {
	char type;
	uint64_t stamp;
	target_ulong pc;
	target_ulong addr;
	uint8_t size;
};

uint64_t curr_stamp;
struct timeval tv;
uint64_t start_seconds;

uint64_t UpdateTimeStamp(void) {
	gettimeofday(&tv,NULL);
	return((((uint64_t)tv.tv_sec)-start_seconds)*(uint64_t)1000000+tv.tv_usec);
}

// This is where we'll write out the memstat data
FILE *plugin_log;

uint64_t bytes_read, bytes_written;
uint64_t num_reads, num_writes;

// Recording
uint64_t record_count;
struct record records[MAX_RECORDS];

int mem_write_callback(CPUState *env, target_ulong pc, target_ulong addr,
						target_ulong size, void *buf) {
	bytes_written += size;
	num_writes++;

/*
	if (record_count%(uint64_t)10000 == 1)
		panda_enable_precise_pc();

	if (record_count%(uint64_t)10000 >= 2)
		panda_disable_precise_pc();
*/

	if (record_count < MAX_RECORDS) {
		records[record_count].stamp = UpdateTimeStamp();
		records[record_count].type = 'W';
		records[record_count].pc = pc;
		records[record_count].addr = addr;
		records[record_count].size = (uint8_t)size;
		record_count++;
	}

	return 1;
}

int mem_read_callback(CPUState *env, target_ulong pc, target_ulong addr,
						target_ulong size, void *buf) {
	bytes_read += size;
	num_reads++;

/*
	if (record_count%(uint64_t)10000 == 1)
		panda_enable_precise_pc();

	if (record_count%(uint64_t)10000 >= 2)
		panda_disable_precise_pc();
*/

	if (record_count < MAX_RECORDS) {
		records[record_count].stamp = UpdateTimeStamp();
		records[record_count].type = 'R';
		records[record_count].pc = pc;
		records[record_count].addr = addr;
		records[record_count].size = (uint8_t)size;
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

	// Setting the starting seconds.
	gettimeofday(&tv,NULL);
	start_seconds = (uint64_t)tv.tv_sec;

// Currently only supported for I386
//#if defined(TARGET_I386)
	panda_cb pcb;

	// Need this to get EIP with our callbacks
	panda_disable_precise_pc();
	// Enable memory logging
	panda_enable_memcb();

	pcb.virt_mem_read = mem_read_callback;
	panda_register_callback(self, PANDA_CB_VIRT_MEM_READ, pcb);
	pcb.virt_mem_write = mem_write_callback;
	panda_register_callback(self, PANDA_CB_VIRT_MEM_WRITE, pcb);
//#else
//	fprintf(stderr, "The nsac_memstats is not supported on this platform.\n");
//	return false;
//#endif
	return true;
}

void uninit_plugin(void *self) {
	uint64_t i;
	for (i = 0; i < record_count; i++) {
		fprintf(plugin_log, "%c:%lu:" TARGET_FMT_lx ":" TARGET_FMT_lx ":%d\n",
				records[i].type,
				records[i].stamp,
				records[i].pc,
				records[i].addr,
				(int)records[i].size);
	}
	fflush(plugin_log);
	fclose(plugin_log);
	printf("Memory statistics: %lu loads, %lu stores, "
		"%lu bytes read, %lu bytes written.\n",
		num_reads, num_writes, bytes_read, bytes_written);
}
