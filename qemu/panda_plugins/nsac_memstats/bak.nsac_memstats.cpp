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

extern "C" {
	#define __STDC_FORMAT_MACROS
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

//#include "../common/prog_point.h"
//#include "../callstack_instr/callstack_instr_ext.h"

#define DEFAULT_LOG_FILE "nsac_memstats.txt"

// These need to be extern "C" so that the ABI is compatible with
// QEMU/PANDA, which is written in C
extern "C" {
	bool init_plugin(void *);
	void uninit_plugin(void *);
	int mem_write_callback(CPUState *env, target_ulong pc, target_ulong addr, target_ulong size, void *buf);
	int mem_read_callback(CPUState *env, target_ulong pc, target_ulong addr, target_ulong size, void *buf);
}

struct record {
	target_ulong pc;
	target_ulong addr;
};

struct database {
	uint64_t record_count;
	struct record records[(uint64_t)10000];
};

// This is where we'll write out the memstat data
FILE *plugin_log;
//void* nsac_memstats_plugin_self;

uint64_t bytes_read, bytes_written;
uint64_t num_reads, num_writes;
uint64_t stamp;
struct timeval tv;
uint64_t start_seconds;
struct database *db;
int recording;
int record_count;
struct record records[10000];

void UpdateTimeStamp(void) {
	gettimeofday(&tv,NULL);
	stamp = (((uint64_t)tv.tv_sec)-start_seconds)*(uint64_t)1000000+tv.tv_usec;
}

int mem_write_callback(CPUState *env, target_ulong pc, target_ulong addr,
						target_ulong size, void *buf) {
	bytes_written += size;
	num_writes++;

#if defined(TARGET_I386)
/*	if (recording == 1 && db->record_count < (uint64_t)10000) {
		struct record rc = {	.pc = (uint64_t)pc,
					.addr = (uint64_t)addr };
		db->records[db->record_count] = rc;
		db->record_count++; */
	if (record_count < 10000) {
		records[record_count].pc = pc;
		records[record_count].addr = addr;
		record_count++;
	} else
		panda_disable_precise_pc();
		


	//prog_point p = {};
	//get_prog_point(env, &p);

//	if (num_writes%100 == 0)
//		panda_enable_precise_pc();
//	else
//		panda_disable_precise_pc();
/*
	UpdateTimeStamp();
	fprintf(plugin_log, "%lu:W:" TARGET_FMT_lx ":" TARGET_FMT_lx ":%lu\n",
				stamp,
				pc,
				addr,
				(uint64_t)size);
*/
#endif

	return 1;
}

int mem_read_callback(CPUState *env, target_ulong pc, target_ulong addr,
						target_ulong size, void *buf) {
	bytes_read += size;
	num_reads++;
/*
#if defined(TARGET_I386)
	//prog_point p = {};
	//get_prog_point(env, &p);

	UpdateTimeStamp();
	fprintf(plugin_log, "%lu:R:" TARGET_FMT_lx ":" TARGET_FMT_lx ":%lu\n",
				stamp,
				pc,
				addr,
				(uint64_t)size);
#endif
*/
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
// Currently only supported for I386
#if defined(TARGET_I386)
	panda_cb pcb;

	//if(!init_callstack_instr_api()) return false;
	// Need this to get EIP with our callbacks
	panda_enable_precise_pc();
	// Enable memory logging
	panda_enable_memcb();

	// Initialise db
/*	recording = 1;
	db = (struct database *)malloc(sizeof(struct database));
	if (!db) {
		recording = 0;
		fprintf(stderr, "MALLOC FAILED\n");
	} */

	// Setting the starting seconds.
	//gettimeofday(&tv,NULL);
	//start_seconds = (uint64_t)tv.tv_sec;

	//pcb.virt_mem_read = mem_read_callback;
	//panda_register_callback(self, PANDA_CB_VIRT_MEM_READ, pcb);
	pcb.virt_mem_write = mem_write_callback;
	panda_register_callback(self, PANDA_CB_VIRT_MEM_WRITE, pcb);

	return true;

#else

	fprintf(stderr, "The nsac_memstats is not supported on this platform.\n");
	return false;

#endif
	//	nsac_memstats_plugin_self = self;
	return true;
}

void uninit_plugin(void *self) {
	panda_disable_precise_pc();
	uint64_t i;
	for (i = 0; i< db->record_count; i++) {
		fprintf(plugin_log, "W:" TARGET_FMT_lx ":" TARGET_FMT_lx "\n",
				records[i].pc,
				records[i].addr);
	}
	fflush(plugin_log);
	fclose(plugin_log);
	free(db);
	printf("Memory statistics: %lu loads, %lu stores, %lu bytes read, %lu bytes written.\n",
			num_reads, num_writes, bytes_read, bytes_written);
}
