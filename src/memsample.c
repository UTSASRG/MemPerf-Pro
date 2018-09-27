#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <asm/unistd.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include "memsample.h"
#include "libmallocprof.h"

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

extern "C" {
	pid_t gettid() {
		return syscall(__NR_gettid);
	}
}

__thread extern thread_data thrData;

thread_local perf_info perfInfo;

//void getPerfInfo(int64_t *fault, int64_t *tlb, int64_t *cache_miss, int64_t *cache_ref, int64_t *instr){
//
//		read(perfInfo.perf_fd_fault, fault, sizeof(int64_t));
//		read(perfInfo.perf_fd_tlb_reads, tlb, sizeof(int64_t));
//		read(perfInfo.perf_fd_cache_miss, cache_miss, sizeof(int64_t));
//		read(perfInfo.perf_fd_cache_ref, cache_ref, sizeof(int64_t));
//		read(perfInfo.perf_fd_instr, instr, sizeof(int64_t));
//}

//get data from PMU and store it into the PerfReadInfo struct
void getPerfInfo (PerfReadInfo* i) {

	read(perfInfo.perf_fd_fault, &(i->faults), sizeof(int64_t));
	read(perfInfo.perf_fd_tlb_reads, &(i->tlb_read_misses), sizeof(int64_t));
	read(perfInfo.perf_fd_tlb_writes, &(i->tlb_write_misses), sizeof(int64_t));
	read(perfInfo.perf_fd_cache_miss, &(i->cache_misses), sizeof(int64_t));
	read(perfInfo.perf_fd_cache_ref, &(i->cache_refs), sizeof(int64_t));
	read(perfInfo.perf_fd_instr, &(i->instructions), sizeof(int64_t));
}

void doPerfRead() {
    int64_t count_fault, count_tlb_reads, count_tlb_writes,
			count_cache_miss, count_cache_ref, count_instr;

	ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_cache_ref, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_DISABLE, 0);

	read(perfInfo.perf_fd_fault, &count_fault, sizeof(int64_t));
	read(perfInfo.perf_fd_tlb_reads, &count_tlb_reads, sizeof(int64_t));
	read(perfInfo.perf_fd_tlb_writes, &count_tlb_writes, sizeof(int64_t));
	read(perfInfo.perf_fd_cache_miss, &count_cache_miss, sizeof(int64_t));
	read(perfInfo.perf_fd_cache_ref, &count_cache_ref, sizeof(int64_t));
	read(perfInfo.perf_fd_instr, &count_instr, sizeof(int64_t));

		//fprintf(thrData.output, "\n>>> tot page faults    %ld\n", count_fault);
		//fprintf(thrData.output, ">>> tot TLB misses     %ld\n", count_tlb);
		//fprintf(thrData.output, ">>> tot cache misses   %ld\n", count_cache_miss);
		//fprintf(thrData.output, ">>> tot cache refs     %ld\n", count_cache_ref);
        //fprintf(thrData.output, ">>> tot miss rate      %f%%\n", (float)count_cache_miss/(float)count_cache_ref * 100 );
		//fprintf(thrData.output, ">>> tot instructions   %ld\n", count_instr);
}

void doPerfRead_noFile () {
    int64_t count_fault, count_tlb, count_cache_miss, count_instr;
		ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_DISABLE, 0);
		ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_DISABLE, 0);
		ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_DISABLE, 0);
		ioctl(perfInfo.perf_fd_cache_ref, PERF_EVENT_IOC_DISABLE, 0);
		ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_DISABLE, 0);

		read(perfInfo.perf_fd_fault, &count_fault, sizeof(int64_t));
		read(perfInfo.perf_fd_tlb_reads, &count_tlb, sizeof(int64_t));
		read(perfInfo.perf_fd_cache_miss, &count_cache_miss, sizeof(int64_t));
		read(perfInfo.perf_fd_cache_ref, &count_cache_miss, sizeof(int64_t));
		read(perfInfo.perf_fd_instr, &count_instr, sizeof(int64_t));
}

void setupSampling(void) {
	struct perf_event_attr pe_fault, pe_tlb_reads, pe_tlb_writes, pe_cache_miss, pe_cache_ref, pe_instr;
	memset(&pe_fault, 0, sizeof(struct perf_event_attr));

	pe_fault.type = PERF_TYPE_SOFTWARE;
	pe_fault.size = sizeof(struct perf_event_attr);

	// config: this field is set depending on the type of bridge in the processor
	// and whether we would like to sample load/store accesses.
	// For more information see the Intel 64 and IA-32 Architectures Software Developer's Manual:
	// http://courses.cs.washington.edu/courses/cse451/15au/readings/ia32-3.pdf
	pe_fault.config = PERF_COUNT_SW_PAGE_FAULTS;

	//This field specifies the format of the data returned by
	//read() on a perf_event_open() file descriptor.
	//pe_fault.read_format = PERF_FORMAT_GROUP;

	//Disabled: whether the counter starts with disabled/enabled status.
	pe_fault.disabled = 0;
	//pe_fault.disabled = 1;

	//Pinned: The pinned bit specifies that the counter should always be on
	//the CPU if at all possible.
	//pe_fault.pinned = 0;

	//Inherit:The inherit bit specifies that this counter should count
	//events of child tasks as well as the task specified.
	//WARNING: It is this property that breaks perf_event_open(&pe, 0, -1...)
	//UPDATE: this is because it does not work with the PERF_FORMAT_GROUP
	//read format flag.
	//pe_fault.inherit = 1;

	//Exclucive:The exclusive bit specifies that when this counter's group is
	//on the CPU, it should be the only group using the CPU's counters.
	//pe_fault.exclusive = 0;

	//Exclude_xxx: Do not sample a specified side of events,
	//user, kernel, or hypevisor
	//pe_fault.exclude_user = 0;
	pe_fault.exclude_kernel = 1;
	pe_fault.exclude_hv = 1;

	//Precise_ip: This controls the amount of skid. See perf_event.h
	pe_fault.precise_ip = 1;
	pe_fault.freq = 0;
	pe_fault.sample_period = 10000;

	//Sample_id_all: TID, TIME, ID, STREAM_ID, and CPU added to every sample.
	pe_fault.sample_id_all = 0;

	//Exclude_xxx: Sample guest/host instances or not.
	pe_fault.exclude_host = 0;
	pe_fault.exclude_guest = 1;

	// Make an exact copy of the pe_fault attributes to be used for the
	// corresponding store events' attributes.
	memcpy(&pe_tlb_reads, &pe_fault, sizeof(struct perf_event_attr));
	memcpy(&pe_tlb_writes, &pe_fault, sizeof(struct perf_event_attr));
	memcpy(&pe_cache_miss, &pe_fault, sizeof(struct perf_event_attr));
	memcpy(&pe_cache_ref, &pe_fault, sizeof(struct perf_event_attr));
    memcpy(&pe_instr, &pe_fault, sizeof(struct perf_event_attr));

	pe_tlb_reads.type = PERF_TYPE_HW_CACHE;
	pe_tlb_reads.config = PERF_COUNT_HW_CACHE_DTLB |
	                      (PERF_COUNT_HW_CACHE_OP_READ << 8) |
						  (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);

	pe_tlb_writes.type = PERF_TYPE_HW_CACHE;
	pe_tlb_writes.config = PERF_COUNT_HW_CACHE_DTLB |
		                   (PERF_COUNT_HW_CACHE_OP_WRITE << 8) |
						   (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);

	pe_cache_miss.type = PERF_TYPE_HARDWARE;
	pe_cache_miss.config = PERF_COUNT_HW_CACHE_MISSES;

    pe_cache_ref.type = PERF_TYPE_HARDWARE;
    pe_cache_ref.config = PERF_COUNT_HW_CACHE_REFERENCES;

    pe_instr.type = PERF_TYPE_HARDWARE;
    pe_instr.config = PERF_COUNT_HW_INSTRUCTIONS;

	// Create the perf_event for this thread on all CPUs with no event group
	// Second parameter (target thread): 0=self, -1=cpu-wide mode
	// Third parameter: cpu: -1 == any
	// Fourth parameter: group id (-1 for group leader)
	perfInfo.perf_fd_fault = perf_event_open(&pe_fault, 0, -1, -1, 0);
	if(perfInfo.perf_fd_fault == -1) {
		perror("Failed to open perf event for pe_fault");
		abort();
	}

	//perf_event_open(0x79e260, 1440, -1, -1, 0x8 /* PERF_FLAG_??? */) = 7
	perfInfo.perf_fd_tlb_reads = perf_event_open(&pe_tlb_reads, 0, -1, -1, 0);
	if(perfInfo.perf_fd_tlb_reads == -1) {
		perror("Failed to open perf event for pe_tlb_reads");
		abort();
	}

	perfInfo.perf_fd_tlb_writes = perf_event_open(&pe_tlb_writes, 0, -1, -1, 0);
	if(perfInfo.perf_fd_tlb_writes == -1) {
		perror("Failed to open perf event for pe_tlb_writes");
		abort();
	}

	perfInfo.perf_fd_cache_miss = perf_event_open(&pe_cache_miss, 0, -1, -1, 0);
	if(perfInfo.perf_fd_cache_miss == -1) {
		perror("Failed to open perf event for pe_cache_miss");
		abort();
	}

    perfInfo.perf_fd_cache_ref = perf_event_open(&pe_cache_ref, 0, -1, -1, 0);
	if(perfInfo.perf_fd_cache_ref == -1) {
		perror("Failed to open perf event for pe_cache_ref");
		abort();
	}

    perfInfo.perf_fd_instr = perf_event_open(&pe_instr, 0, -1, -1, 0);
	if(perfInfo.perf_fd_instr == -1) {
		perror("Failed to open perf event for pe_instr");
		abort();
	}

	ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_RESET, 0);
	ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_RESET, 0);
	ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_RESET, 0);
	ioctl(perfInfo.perf_fd_cache_ref, PERF_EVENT_IOC_RESET, 0);
	ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_RESET, 0);

	ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(perfInfo.perf_fd_cache_ref, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_ENABLE, 0);


	/*
	if(ioctl(perfInfo.perf_fd_cache, PERF_EVENT_IOC_SET_OUTPUT,
				perfInfo.perf_fd_cache) == -1) {
		perror("Call to ioctl failed");
		abort();
	}
	*/
}

int initSampling(void) {
	// Setup the sampling
	setupSampling();

	return 0;
}
