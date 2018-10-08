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

#define PERF_GROUP_SIZE 5

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

extern "C" {
	pid_t gettid() {
		return syscall(__NR_gettid);
	}
}

extern __thread thread_data thrData;

thread_local perf_info perfInfo;

struct read_format {
	uint64_t nr;
	struct {
		uint64_t value;
	} values[PERF_GROUP_SIZE];
};

inline int create_perf_event(perf_event_attr * attr, int group) {
	int fd = perf_event_open(attr, 0, -1, group, 0);
	if(fd == -1) {
		perror("Failed to open perf event");
		abort();
	}
	return fd;
}

//get data from PMU and store it into the PerfReadInfo struct
void getPerfInfo (PerfReadInfo * i) {
	#ifdef NO_PMU
	#warning NO_PMU flag set -> sampling will be disabled
	return;
	#endif

	struct read_format buffer1, buffer2;

	read(perfInfo.perf_fd_fault, &buffer1, sizeof(struct read_format));
	read(perfInfo.perf_fd_instr, &buffer2, sizeof(struct read_format));

	i->faults = buffer1.values[0].value;
	i->tlb_read_misses = buffer1.values[1].value;
	i->tlb_write_misses = buffer1.values[2].value;
	i->cache_misses = buffer1.values[3].value;
	i->cache_refs = buffer1.values[4].value;
	i->instructions = buffer2.values[0].value;

	/*
	// DEBUG OUTPUT
	fprintf(stderr, "nr1 = %ld, nr2 = %ld\n", buffer1.nr, buffer2.nr);
	fprintf(stderr, "  faults           = %ld\n", buffer1.values[0].value);
	fprintf(stderr, "  tlb read misses  = %ld\n", buffer1.values[1].value);
	fprintf(stderr, "  tlb write misses = %ld\n", buffer1.values[2].value);
	fprintf(stderr, "  cache misses     = %ld\n", buffer1.values[3].value);
	fprintf(stderr, "  cache refs       = %ld\n", buffer1.values[4].value);
	fprintf(stderr, "  instructions     = %ld\n", buffer2.values[0].value);
	*/
}

void doPerfRead() {
	#ifdef NO_PMU
	return;
	#endif
	PerfReadInfo perf;
	getPerfInfo(&perf);

	ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_tlb_writes, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_cache_ref, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_DISABLE, 0);

	if(thrData.output) {
			fprintf(thrData.output, "\n");
			fprintf(thrData.output, ">>> tot page faults      %ld\n", perf.faults);
			fprintf(thrData.output, ">>> tot TLB read misses  %ld\n", perf.tlb_read_misses);
			fprintf(thrData.output, ">>> tot TLB write misses %ld\n", perf.tlb_write_misses);
			fprintf(thrData.output, ">>> tot cache misses     %ld\n", perf.cache_misses);
			fprintf(thrData.output, ">>> tot cache refs       %ld\n", perf.cache_refs);
			fprintf(thrData.output, ">>> tot miss rate        %f%%\n", (float)perf.cache_misses/(float)perf.cache_refs * 100 );
			fprintf(thrData.output, ">>> tot instructions     %ld\n", perf.instructions);
	}
}

void setupSampling(void) {
	#ifdef NO_PMU
	return;
	#endif

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
	pe_fault.read_format = PERF_FORMAT_GROUP;

	//Disabled: whether the counter starts with disabled/enabled status.
	pe_fault.disabled = 1;

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
	pe_fault.exclude_user = 0;
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
	pe_tlb_reads.disabled = 0;

	pe_tlb_writes.type = PERF_TYPE_HW_CACHE;
	pe_tlb_writes.config = PERF_COUNT_HW_CACHE_DTLB |
		                   (PERF_COUNT_HW_CACHE_OP_WRITE << 8) |
						   (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
	pe_tlb_writes.disabled = 0;

	pe_cache_miss.type = PERF_TYPE_HARDWARE;
	pe_cache_miss.config = PERF_COUNT_HW_CACHE_MISSES;
	pe_cache_miss.disabled = 0;

	pe_cache_ref.type = PERF_TYPE_HARDWARE;
	pe_cache_ref.config = PERF_COUNT_HW_CACHE_REFERENCES;
	pe_cache_ref.disabled = 0;

	pe_instr.type = PERF_TYPE_HARDWARE;
	pe_instr.config = PERF_COUNT_HW_INSTRUCTIONS;
	pe_instr.disabled = 0;

	// Create the perf_event for this thread on all CPUs with no event group
	// Second parameter (target thread): 0=self, -1=cpu-wide mode
	// Third parameter: cpu: -1 == any
	// Fourth parameter: group id (-1 for group leader)
	// *** WARNING ***
	// DO NOT change the order of the following perf_event_open system calls!
	// Doing so will change the order of their values when read from the group
	// leader's FD, which occurs elsewhere, and will thus be incorrect unless
	// similarly reordered.
	// *** *** *** ***
	perfInfo.perf_fd_fault = create_perf_event(&pe_fault, -1);
	perfInfo.perf_fd_tlb_reads = create_perf_event(&pe_tlb_reads, perfInfo.perf_fd_fault);
	perfInfo.perf_fd_tlb_writes = create_perf_event(&pe_tlb_writes, perfInfo.perf_fd_fault);
	perfInfo.perf_fd_cache_miss = create_perf_event(&pe_cache_miss, perfInfo.perf_fd_fault);
	perfInfo.perf_fd_cache_ref = create_perf_event(&pe_cache_ref, perfInfo.perf_fd_fault);
	perfInfo.perf_fd_instr = create_perf_event(&pe_instr, -1);

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
}

int initSampling(void) {
	#ifndef NO_PMU
	setupSampling();
	#endif

	return 0;
}
