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
#include "libmallocprof.h"

#define PERF_GROUP_SIZE 5

long long perf_mmap_read();
long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

pthread_spinlock_t _perf_spin_lock;
extern thread_local thread_data thrData;
thread_local perf_info perfInfo;
thread_local bool isCountingInit = false;
thread_local bool isSamplingInit = false;
int sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TIME | PERF_SAMPLE_TID |
									PERF_SAMPLE_ADDR | PERF_SAMPLE_DATA_SRC;

int read_format = PERF_FORMAT_GROUP;

struct read_format {
	uint64_t nr;
	struct {
		uint64_t value;
	} values[PERF_GROUP_SIZE];
};

inline void acquireGlobalPerfLock() {
		pthread_spin_lock(&_perf_spin_lock);
}

inline void releaseGlobalPerfLock() {
		pthread_spin_unlock(&_perf_spin_lock);
}

inline int create_perf_event(perf_event_attr * attr, int group) {
	int fd = perf_event_open(attr, 0, -1, group, 0);
	if(fd == -1) {
		perror("Failed to open perf event");
		abort();
	}
	return fd;
}

//get data from PMU and store it into the PerfReadInfo struct
void getPerfCounts (PerfReadInfo * i, bool enableCounters) {
	#ifdef NO_PMU
	#warning NO_PMU flag set -> sampling will be disabled
	return;
	#endif

	if(!isCountingInit) {
			return;
	}

	if(enableCounters) {
			ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_RESET, 0);
			ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_RESET, 0);
			ioctl(perfInfo.perf_fd_tlb_writes, PERF_EVENT_IOC_RESET, 0);
			ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_RESET, 0);
			ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_RESET, 0);

			ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_ENABLE, 0);
			//ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_ENABLE, 0);
			//ioctl(perfInfo.perf_fd_tlb_writes, PERF_EVENT_IOC_ENABLE, 0);
			//ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_ENABLE, 0);
			//ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_ENABLE, 0);
			return;
	}

	struct read_format buffer;

	if(read(perfInfo.perf_fd_fault, &buffer, sizeof(struct read_format)) == -1) {
				perror("perf read failed");
	}

	i->faults = buffer.values[0].value;
	i->tlb_read_misses = buffer.values[1].value;
	i->tlb_write_misses = buffer.values[2].value;
	i->cache_misses = buffer.values[3].value;
	i->instructions = buffer.values[4].value;

	if(!enableCounters) {
			ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_DISABLE, 0);
			//ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_DISABLE, 0);
			//ioctl(perfInfo.perf_fd_tlb_writes, PERF_EVENT_IOC_DISABLE, 0);
			//ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_DISABLE, 0);
			//ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_DISABLE, 0);
	}

}

void doPerfCounterRead() {
	#ifdef NO_PMU
	return;
	#endif
	PerfReadInfo perf;
	getPerfCounts(&perf, false);

	ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_tlb_writes, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_DISABLE, 0);

	/*
	// Commented this out because it doesn't work, particularly now that we are resetting/enabling/disabling
	// the PMU counters between function calls, and thus we can no longer obtain a grand total of events
	// from reading their counter values. Even prior to this changeover, only the main thread is producing an
	// output file (at least with our default settings), and thus these totals would only apply to the core
	// the main thread is running on. In fact, there is no reason to call this function anymore at all,
	// other than to disable the counters (which should already be disabled at the time this is called).	-- SAS
	if(thrData.output) {
			fprintf(thrData.output, "\n");
			fprintf(thrData.output, ">>> total page faults        %ld\n", perf.faults);
			fprintf(thrData.output, ">>> total TLB read misses    %ld\n", perf.tlb_read_misses);
			fprintf(thrData.output, ">>> total TLB write misses   %ld\n", perf.tlb_write_misses);
			fprintf(thrData.output, ">>> total cache misses       %ld\n", perf.cache_misses);
			fprintf(thrData.output, ">>> total instructions       %ld\n", perf.instructions);
	}
	*/
}

void setupCounting(void) {
	#ifdef NO_PMU
	return;
	#endif

	if(isCountingInit) {
			return;
	}
	isCountingInit = true;

	struct perf_event_attr pe_fault, pe_tlb_reads, pe_tlb_writes, pe_cache_miss, pe_instr;
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
	pe_fault.freq = 1;
	pe_fault.sample_freq = 10000;
	//pe_fault.freq = 0;
	//pe_fault.sample_period = 10000;

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

	pe_instr.type = PERF_TYPE_HARDWARE;
	pe_instr.config = PERF_COUNT_HW_INSTRUCTIONS;
	pe_instr.disabled = 0;

	// *** WARNING ***
	// DO NOT change the order of the following create_perf_event system calls!
	// Doing so will change the order of their values when read from the group
	// leader's FD, which occurs elsewhere, and will thus be incorrect unless
	// similarly reordered.
	// *** *** *** ***
	perfInfo.perf_fd_fault = create_perf_event(&pe_fault, -1);
	perfInfo.perf_fd_tlb_reads = create_perf_event(&pe_tlb_reads, perfInfo.perf_fd_fault);
	perfInfo.perf_fd_tlb_writes = create_perf_event(&pe_tlb_writes, perfInfo.perf_fd_fault);
	perfInfo.perf_fd_cache_miss = create_perf_event(&pe_cache_miss, perfInfo.perf_fd_fault);
	perfInfo.perf_fd_instr = create_perf_event(&pe_instr, perfInfo.perf_fd_fault);

	ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_RESET, 0);
	ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_RESET, 0);
	ioctl(perfInfo.perf_fd_tlb_writes, PERF_EVENT_IOC_RESET, 0);
	ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_RESET, 0);
	ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_RESET, 0);

	ioctl(perfInfo.perf_fd_fault, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(perfInfo.perf_fd_tlb_reads, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(perfInfo.perf_fd_tlb_writes, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(perfInfo.perf_fd_cache_miss, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(perfInfo.perf_fd_instr, PERF_EVENT_IOC_ENABLE, 0);
}

void sampleHandler(int signum, siginfo_t *info, void *p) {
  #ifndef NDEBUG
  perfInfo->numSignalsRecvd++;
  #endif

  // If the overflow counter has reached zero (indicated by the POLL_HUP code),
  // read the sample data and reset the overflow counter to start again.
  if(info->si_code == POLL_HUP) {
			doSampleRead();
			ioctl(perfInfo.perf_fd, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL);
			ioctl(perfInfo.perf_fd2, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL);
  }
}

void doSampleRead() {
  if(!perfInfo.initialized) {
    return;
  }

  #ifndef NDEBUG
  perfInfo->numSampleReadOps++;
  //fprintf(stderr, "thread %d numSampleReadOps++ == %ld -> %ld\n", current->index, perfInfo->numSampleReadOps, ++perfInfo->numSampleReadOps);
  #endif

  perfInfo.prev_head = perf_mmap_read();
}

void setupSampling() {
	// Create the perf_event for this thread on all CPUs with no event group
	// Second parameter (target thread): 0=self, -1=cpu-wide mode
	// Third parameter: cpu: -1 == any
	// Fourth parameter: group id (-1 for group leader)
	struct perf_event_attr pe_load, pe_store;
	memset(&pe_load, 0, sizeof(struct perf_event_attr));

	pe_load.type = PERF_TYPE_RAW;
	pe_load.size = sizeof(struct perf_event_attr);
	pe_load.config = LOAD_ACCESS;

	int sign = rand() % 2;
	int percent = rand() % 1001;  // generates between 0 and 1000, representing 0% to 10%, respectively
	float fraction = percent / 10000.0; 
	if(sign) {
			fraction *= -1;
	}
	int fuzzed_period = SAMPLING_PERIOD * (1 + fraction);

 	pe_load.sample_period = fuzzed_period;
	pe_load.freq = 0;
	pe_load.sample_type = sample_type;
	pe_load.read_format = read_format;
	pe_load.disabled = 1;
	pe_load.pinned = 0;
	pe_load.inherit = 0;
	pe_load.exclusive = 0;
	pe_load.exclude_user = 0;
	pe_load.exclude_kernel = 1;
	pe_load.exclude_hv = 1;
	pe_load.exclude_idle = 1;
	pe_load.mmap = 0;
	pe_load.comm = 0;
	pe_load.inherit_stat = 0;
	pe_load.enable_on_exec = 1;
	pe_load.task = 0;
	pe_load.watermark = 0;
	pe_load.precise_ip = 1;
	pe_load.mmap_data = 0;
	pe_load.sample_id_all = 0;
	pe_load.exclude_host = 0;
	pe_load.exclude_guest = 1;
	pe_load.exclude_callchain_kernel = 1; // exclude kernel callchains
	pe_load.exclude_callchain_user = 1; // exclude user callchains
	pe_load.mmap2 = 0;
	pe_load.comm_exec = 0; // flag comm events that are due to an exec
	pe_load.wakeup_events = 0;
	pe_load.wakeup_watermark = 0;
	pe_load.bp_len = 0;
	pe_load.branch_sample_type = 0;
	pe_load.sample_regs_user = 0;
	pe_load.sample_stack_user = 0;

	memcpy(&pe_store, &pe_load, sizeof(struct perf_event_attr));
	pe_store.config = STORE_ACCESS;
	acquireGlobalPerfLock();
	perfInfo.perf_fd2 = perf_event_open(&pe_store, 0, -1, -1, 0);
	if(perfInfo.perf_fd2 == -1) {
			fprintf(stderr, "Failed to open perf event for pe_store\n");
			abort();
	}
	perfInfo.perf_fd = perf_event_open(&pe_load, 0, -1, -1, 0);
	if(perfInfo.perf_fd == -1) {
			fprintf(stderr, "Failed to open perf event for pe_load\n");
			abort();
	}
	releaseGlobalPerfLock();

	// Setting up memory to pass information about a trap
	if((perfInfo.ring_buf = mmap(NULL, MAPSIZE, PROT_READ | PROT_WRITE,
					MAP_SHARED, perfInfo.perf_fd, 0)) == MAP_FAILED) {
			fprintf(stderr, "mmap failed on tid=%d, perf_fd=%d: %s\n",
							perfInfo.tid, perfInfo.perf_fd, strerror(errno));
			abort();
	}
	perfInfo.ring_buf_data_start = (void *)((uintptr_t)perfInfo.ring_buf + PAGESIZE);

	// Set the perf_event file to async mode
	if(fcntl(perfInfo.perf_fd, F_SETFL, O_RDWR | O_NONBLOCK | O_ASYNC) == -1) {
			fprintf(stderr, "Failed to set perf event file to ASYNC mode\n");
			abort();
	}

	if(ioctl(perfInfo.perf_fd2, PERF_EVENT_IOC_SET_OUTPUT,
				perfInfo.perf_fd) == -1) {
			fprintf(stderr, "Call to ioctl failed\n");
			abort();
	}

	// Deliver the signal to this thread
	struct f_owner_ex owner = {F_OWNER_TID, perfInfo.tid};
	if(fcntl(perfInfo.perf_fd, F_SETOWN_EX, &owner) == -1) {
			fprintf(stderr, "Failed to set the owner of the perf event file\n");
			abort();
	}
	if(fcntl(perfInfo.perf_fd2, F_SETOWN_EX, &owner) == -1) {
			fprintf(stderr, "Failed to set the owner of the perf event file\n");
			abort();
	}

	// Tell the file to send a SIGIO when an event occurs
	if(fcntl(perfInfo.perf_fd, F_SETSIG, SIGIO) == -1) {
			fprintf(stderr, "Failed to set perf event file's async signal\n");
			abort();
	}
	if(fcntl(perfInfo.perf_fd2, F_SETSIG, SIGIO) == -1) {
			fprintf(stderr, "Failed to set perf event file's async signal\n");
			abort();
	}

	perfInfo.initialized = true;

	if(ioctl(perfInfo.perf_fd, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL) == -1) {
			fprintf(stderr, "Failed to refresh perf event w/ fd %d, line %d: %s\n",
							perfInfo.perf_fd, __LINE__, strerror(errno));
			abort();
	}
	if(ioctl(perfInfo.perf_fd2, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL) == -1) {
			fprintf(stderr, "Failed to refresh perf event w/ fd %d, line %d: %s\n",
							perfInfo.perf_fd2, __LINE__, strerror(errno));
			abort();
	}

	if(ioctl(perfInfo.perf_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
			fprintf(stderr, "Failed to enable perf event w/ fd %d, line %d: %s\n",
							perfInfo.perf_fd, __LINE__, strerror(errno));
			abort();
	}
	if(ioctl(perfInfo.perf_fd2, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
			fprintf(stderr, "Failed to enable perf event w/ fd %d, line %d: %s\n",
							perfInfo.perf_fd2, __LINE__, strerror(errno));
			abort();
	}
}

void stopCounting(void) {
		if(!isCountingInit) {
				return;
		}

		isCountingInit = false;

    close(perfInfo.perf_fd_fault);
    close(perfInfo.perf_fd_tlb_reads);
    close(perfInfo.perf_fd_tlb_writes);
    close(perfInfo.perf_fd_cache_miss);
    close(perfInfo.perf_fd_instr);
}

void stopSampling(void) {
		if(!isSamplingInit) {
				return;
		}

		isSamplingInit = false;

		if(ioctl(perfInfo.perf_fd, PERF_EVENT_IOC_DISABLE, 0) == -1) {
				fprintf(stderr, "Failed to disable perf event: %s\n", strerror(errno));
		}
		if(ioctl(perfInfo.perf_fd2, PERF_EVENT_IOC_DISABLE, 0) == -1) {
				fprintf(stderr, "Failed to disable perf event: %s\n", strerror(errno));
		}
		// process any sample data still remaining in the ring buffer
		doSampleRead();

		// Relinquish our mmap'd memory and close perf file descriptors
		if(munmap(perfInfo.ring_buf, MAPSIZE)) {
				fprintf(stderr, "Unable to unmap perf ring buffer\n");
		}
		if(close(perfInfo.perf_fd) == -1) {
				fprintf(stderr, "Unable to close perf_fd file descriptor: %s\n", strerror(errno));
		}
		if(close(perfInfo.perf_fd2) == -1) {
				fprintf(stderr, "Unable to close perf_fd2 file descriptor: %s\n", strerror(errno));
		}
}

int initPMU(void) {
		if(isSamplingInit) {
				return -1;
		} else {
				isSamplingInit = true;
		}

		pthread_spin_init(&_perf_spin_lock, PTHREAD_PROCESS_PRIVATE);

		perfInfo.tid = gettid();

		struct sigaction sa;
		sigemptyset(&sa.sa_mask);
		sigaddset(&sa.sa_mask, SIGUSR2);
		sigaddset(&sa.sa_mask, SIGRTMIN);
		sa.sa_sigaction = sampleHandler;
		sa.sa_flags = SA_SIGINFO;

		if(sigaction(SIGIO, &sa, NULL) != 0) {
				fprintf(stderr, "failed to set SIGIO handler: %s\n", strerror(errno));
				abort();
		}

		perfInfo.data_buf_copy = (char *)mmap(NULL, DATA_MAPSIZE, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if(perfInfo.data_buf_copy == MAP_FAILED) {
				fprintf(stderr, "mmap failed in %s\n", __FUNCTION__);
				abort();
		}

		#ifdef NO_PMU
				#warning MEMORY ACCESS SAMPLING IS DISABLED
		#else
		setupCounting();
		setupSampling();
		#endif

		return 0;
}

long long perf_mmap_read() {
	struct perf_event_header *event;
	struct perf_event_mmap_page *control_page =
		(struct perf_event_mmap_page *)perfInfo.ring_buf;
	uint64_t copy_amt, head, offset, prev_head_wrapped, size;
	void * data_mmap = perfInfo.ring_buf_data_start;
	char * use_data_buf = NULL;

	if(control_page == NULL) {
			fprintf(stderr, "control_page is NULL; ring_buf=%p, "
							"data_mmap=%p\n", perfInfo.ring_buf, data_mmap);
			abort();
	}

	head = control_page->data_head;
	size = head - perfInfo.prev_head;

	if(head < perfInfo.prev_head)
		perfInfo.prev_head = 0;

	if((DATA_MAPSIZE - PAGESIZE) < (ssize_t)size) {
		fprintf(stderr, "sample data size is dangerously close "
				"to buffer size; data loss is likely to occur\n");
	}

	prev_head_wrapped = perfInfo.prev_head % DATA_MAPSIZE;

	copy_amt = size;

	// If we have to wrap around to the beginning of the ring buffer in order
	// to copy all of the new data, start by copying from prev_head_wrapped
	// to the end of the buffer.
	if(size > (DATA_MAPSIZE - prev_head_wrapped)) {
			copy_amt = DATA_MAPSIZE - prev_head_wrapped;

			memcpy(perfInfo.data_buf_copy, ((char *)data_mmap + prev_head_wrapped), copy_amt);

			memcpy(perfInfo.data_buf_copy + copy_amt,
							(char *)data_mmap, (size - copy_amt));

			use_data_buf = perfInfo.data_buf_copy;
	} else {
			use_data_buf = (char *)data_mmap + prev_head_wrapped;
	}

	offset = 0;
	while(offset < size) {
		long long starting_offset = offset;
		//uint64_t ip = 0;
		uint64_t intpaddr = 0;
		eMemAccessType accessType = E_MEM_UNKNOWN;

		event = (struct perf_event_header *)&use_data_buf[offset];

		// move position past the header we just read above
		offset += sizeof(struct perf_event_header); 

		// Sample data
		if(event->type == PERF_RECORD_SAMPLE) {
			if(sample_type & PERF_SAMPLE_IP) {
				//memcpy(&ip, &use_data_buf[offset], sizeof(uint64_t));
				//ip = *(uint64_t *)(use_data_buf + offset);
				offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_TID) {
				/*
				uint32_t pid, tid;
				pid = *(uint32_t *)(use_data_buf + offset);
				tid = *(uint32_t *)(use_data_buf + offset + sizeof(uint32_t));
				//memcpy(&pid, &use_data_buf[offset], sizeof(uint32_t));
				//memcpy(&tid, &use_data_buf[(offset + sizeof(uint32_t))], sizeof(uint32_t));
				*/
				offset += 2 * sizeof(uint32_t);
			}

			if(sample_type & PERF_SAMPLE_TIME) {
					offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_ADDR) {
					//memcpy(&intpaddr, &use_data_buf[offset], sizeof(uint64_t));
					intpaddr = *(uint64_t *)(use_data_buf + offset);
					offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_DATA_SRC) {
        //uint64_t src;
        //memcpy(&src, &use_data_buf[offset], sizeof(uint64_t));
				uint64_t src = *(uint64_t *)(use_data_buf + offset);
        offset += sizeof(uint64_t);

        if(src & (PERF_MEM_OP_LOAD))
          accessType = E_MEM_LOAD;
        else if(src & (PERF_MEM_OP_STORE))
          accessType = E_MEM_STORE;
        else if(src & (PERF_MEM_OP_PFETCH))
          accessType = E_MEM_PFETCH;
        else if(src & (PERF_MEM_OP_EXEC))
          accessType = E_MEM_EXEC;
        else
          accessType = E_MEM_UNKNOWN;
			}
		} else if(event->type == PERF_RECORD_LOST) {
				// If this is the first time we have lost sample data
				// then emit a warning message
				if(!perfInfo.samplesLost) {
						fprintf(stderr, "perf sample data has been lost! "
										"increase your buffer size or read sample data more "
										"often\n");
						perfInfo.samplesLost = true;
				}
		}

		if((intpaddr > 0) && (intpaddr < LAST_USER_ADDR)) {
				ShadowMemory::doMemoryAccess(intpaddr, accessType);
		}

		// Move the offset counter ahead by the size given in the event header.
		offset = starting_offset + event->size;

		#ifndef NDEBUG
		perfInfo.numSamples++;
		#endif
	}
	//PRDBG("thread %d finished reading samples, size = %ld, offset = %ld", getThreadIndex(&size), size, offset);

	// Tell perf where we left off reading; this prevents
	// perf from overwriting data we have not read yet.
	control_page->data_tail = head;

	return head;
}
