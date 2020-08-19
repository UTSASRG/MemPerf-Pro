#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "memsample.h"

#define PERF_GROUP_SIZE 3


long long perf_mmap_read();
long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

//pthread_spinlock_t _perf_spin_lock;
spinlock _perf_spin_lock;
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

struct read_cache_misses_outside_format {
    uint64_t nr;
    struct {
        uint64_t value;
    } values[1];
};

inline void acquireGlobalPerfLock() {
    _perf_spin_lock.lock();
}

inline void releaseGlobalPerfLock() {
//		pthread_spin_unlock(&_perf_spin_lock);
    _perf_spin_lock.unlock();
}

inline int create_perf_event(perf_event_attr * attr, int group) {
	int fd = perf_event_open(attr, 0, -1, group, 0);
	if(fd == -1) {
		fprintf(stderr, "Failed to open perf event %s\n", strerror(errno));
        abort();
	}
	return fd;
}

//get data from PMU and store it into the PerfReadInfo struct
void getPerfCounts (PerfReadInfo * i) {

#ifdef NO_PMU
#warning NO_PMU flag set -> sampling will be disabled
    return;
#endif

    if (!isCountingInit) {
        return;
    }

    struct read_format buffer;

    if (read(perfInfo.perf_fd_fault, &buffer, sizeof(struct read_format)) == -1) {
        perror("perf read failed");
    }

    memcpy(i, &(buffer.values), sizeof(buffer.values));

}

void setupCounting(void) {
	#ifdef NO_PMU
	return;
	#endif

	if(isCountingInit) {
			return;
	}
	isCountingInit = true;

	struct perf_event_attr pe_fault;
    struct perf_event_attr pe_cache;
    struct perf_event_attr pe_instr;
	memset(&pe_fault, 0, sizeof(struct perf_event_attr));

    pe_fault.type = PERF_TYPE_SOFTWARE;
    pe_fault.size = sizeof(struct perf_event_attr);
    pe_fault.config = PERF_COUNT_SW_PAGE_FAULTS;

	//This field specifies the format of the data returned by
	//read() on a perf_event_open() file descriptor.
    pe_fault.read_format = PERF_FORMAT_GROUP;

	//Disabled: whether the counter starts with disabled/enabled status.
    pe_fault.disabled = 0;
	// Make an exact copy of the pe_fault attributes to be used for the
    // corresponding store events' attributes.

	//Pinned: The pinned bit specifies that the counter should always be on
	//the CPU if at all possible.
//	pe_fault.pinned = 0;
//
	//Inherit:The inherit bit specifies that this counter should count
	//events of child tasks as well as the task specified.
	//WARNING: It is this property that breaks perf_event_open(&pe, 0, -1...)
	//UPDATE: this is because it does not work with the PERF_FORMAT_GROUP
	//read format flag.
//	pe_fault.inherit = 1;

	//Exclucive:The exclusive bit specifies that when this counter's group is
	//on the CPU, it should be the only group using the CPU's counters.
	pe_fault.exclusive = 0;

	//Exclude_xxx: Do not sample a specified side of events,
	//user, kernel, or hypevisor

    pe_fault.exclude_user = 0;
    pe_fault.exclude_kernel = 1;
    pe_fault.exclude_hv = 1;

    //Precise_ip: This controls the amount of skid. See perf_event.h

    pe_fault.precise_ip = 1;

    pe_fault.freq = 1;
    pe_fault.sample_freq = 1;

    //Sample_id_all: TID, TIME, ID, STREAM_ID, and CPU added to every sample.
    pe_fault.sample_id_all = 0;

    //Exclude_xxx: Sample guest/host instances or not.
    pe_fault.exclude_host = 0;
    pe_fault.exclude_guest = 1;

    memcpy(&pe_cache, &pe_fault, sizeof(struct perf_event_attr));
    memcpy(&pe_instr, &pe_fault, sizeof(struct perf_event_attr));

    pe_cache.type = PERF_TYPE_HW_CACHE;
    pe_cache.config = (PERF_COUNT_HW_CACHE_L1D) | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                      (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);

    pe_instr.type = PERF_TYPE_HARDWARE;
	pe_instr.config = PERF_COUNT_HW_INSTRUCTIONS;

	perfInfo.perf_fd_fault = create_perf_event(&pe_fault, -1);
    perfInfo.perf_fd_cache = create_perf_event(&pe_cache, perfInfo.perf_fd_fault);
    perfInfo.perf_fd_instr = create_perf_event(&pe_instr, perfInfo.perf_fd_fault);

}

void sampleHandler(int signum, siginfo_t *info, void *p) {
  #ifndef NDEBUG
    perfInfo->numSignalsRecvd++;
  #endif

    if(!perfInfo.initialized || !isSamplingInit) {
        return;
    }
    if(info->si_code == POLL_HUP) {
        Predictor::outsideCyclesStop();
			doSampleRead();
			ioctl(perfInfo.perf_fd, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL);
			ioctl(perfInfo.perf_fd2, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL);
        Predictor::outsideCycleStart();
  }
}

void doSampleRead() {

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

    pe_load.sample_freq = fuzzed_period;
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
    close(perfInfo.perf_fd_cache);
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

    _perf_spin_lock.init();


		perfInfo.tid = syscall(__NR_gettid);

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
//		setupCounting();
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
		fprintf(stderr, "sample data size is dangerously close to buffer size; data loss is likely to occur\n");
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
				offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_TID) {
				offset += 2 * sizeof(uint32_t);
			}

			if(sample_type & PERF_SAMPLE_TIME) {
					offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_ADDR) {
					intpaddr = *(uint64_t *)(use_data_buf + offset);
					offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_DATA_SRC) {
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
