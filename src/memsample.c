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

int64_t get_trace_count(int fd);

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

extern "C" {
	pid_t gettid() {
		return syscall(__NR_gettid);
	}
	extern void processFreeQueue();
	bool isWordMallocHeader(long *word);
}
__thread extern bool isMainThread;
__thread extern char * shadow_mem;
__thread extern char * stackStart;
__thread extern char * stackEnd;
__thread extern char * watchStartByte;
__thread extern char * watchEndByte;
__thread extern FILE * output;

int numSamples;
int numSignals;
int numHits;

__thread int perf_fd, perf_fd2;
__thread long long prev_head;
__thread pid_t tid;
__thread unsigned char *data;
__thread void *our_mmap;

int sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
					PERF_SAMPLE_ADDR | PERF_SAMPLE_CPU | PERF_SAMPLE_DATA_SRC |
					PERF_SAMPLE_WEIGHT;

int read_format = PERF_FORMAT_GROUP;

void doSampleRead();
long long perf_mmap_read(long long, long long, void *);

void startSampling() {
	if(ioctl(perf_fd, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL) == -1) {
		fprintf(stderr, "Failed to refresh perf event w/ fd %d, line %d: %s\n",
			perf_fd, __LINE__, strerror(errno));
		abort();
	}
	if(ioctl(perf_fd2, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL) == -1) {
		fprintf(stderr, "Failed to refresh perf event w/ fd %d, line %d: %s\n",
			perf_fd2, __LINE__, strerror(errno));
		abort();
	}
	if(ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
		fprintf(stderr, "Failed to enable perf event w/ fd %d, line %d: %s\n",
			perf_fd, __LINE__, strerror(errno));
		abort();
	}
	if(ioctl(perf_fd2, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
		fprintf(stderr, "Failed to enable perf event w/ fd %d, line %d: %s\n",
			perf_fd2, __LINE__, strerror(errno));
		abort();
	}
}

void stopSampling() {
	if(ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0) == -1) {
		fprintf(stderr, "Failed to disable perf event: %s\n", strerror(errno));
		abort();
	}
	if(ioctl(perf_fd2, PERF_EVENT_IOC_DISABLE, 0) == -1) {
		fprintf(stderr, "Failed to disable perf event: %s\n", strerror(errno));
		abort();
	}

	// process any sample data still remaining in the ring buffer
	doSampleRead();

	fprintf(output, ">>> numHits = %d\n", numHits);
}

void doSampleRead() {
	prev_head = perf_mmap_read(prev_head, 0, NULL);

	//processFreeQueue();
}

long long perf_mmap_read(long long prev_head, long long reg_mask,
		void *validate) {
	struct perf_event_header *event;
	struct perf_event_mmap_page *control_page =
		(struct perf_event_mmap_page *)our_mmap;
	long long head, offset;
	long long copy_amt, prev_head_wrap;
	void *data_mmap = (void *)((size_t)our_mmap + getpagesize());
	bool const debug = false;
	int i, size;

	if(control_page == NULL) {
		fprintf(stderr, "ERROR: control_page is NULL; our_mmap=%p, "
					"data_mmap=%p\n", our_mmap, data_mmap);
		abort();
	}

	head = control_page->data_head;
	size = head - prev_head;

	if(head < prev_head)
		prev_head = 0;

	if((DATA_MAPSIZE - size) < 128) {
		fprintf(stderr, "warning: sample data size is dangerously close to "
				"buffer size; data loss is likely to occur\n");
	}
	if(size > DATA_MAPSIZE) {
		fprintf(stderr, "error: we overflowed the mmap buffer with %d > %d "
					"bytes\n", size, DATA_MAPSIZE);
	}

	prev_head_wrap = prev_head % DATA_MAPSIZE;

	copy_amt = size;

	// If we have to wrap around to the beginning of the ring buffer in order
	// to copy all of the new data, start by copying from prev_head_wrap
	// to the end of the buffer.
	if(size > (DATA_MAPSIZE - prev_head_wrap))
		copy_amt = DATA_MAPSIZE - prev_head_wrap;

	memcpy(data, ((unsigned char *)data_mmap + prev_head_wrap), copy_amt);

	if(size > (DATA_MAPSIZE - prev_head_wrap)) {
		memcpy(data + (DATA_MAPSIZE - prev_head_wrap),
				(unsigned char *)data_mmap, (size - copy_amt));
	}

	offset = 0;
	while(offset < size) {
		long long starting_offset = offset;

		event = (struct perf_event_header *)&data[offset];

		// move position past the header we just read above
		offset += sizeof(struct perf_event_header); 

		switch(event->type) {

		// Sample data
		case PERF_RECORD_SAMPLE: {
			uint64_t ip;
			if(sample_type & PERF_SAMPLE_IP) {
				memcpy(&ip, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_TID) {
				uint32_t pid, tid;
				memcpy(&pid, &data[offset], sizeof(uint32_t));
				memcpy(&tid, &data[(offset + sizeof(uint32_t))], sizeof(uint32_t));
				offset += 2 * sizeof(uint32_t);
			}

			if(sample_type & PERF_SAMPLE_TIME) {
				uint64_t time;
				memcpy(&time, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_ADDR) {
				uint64_t addr;
				memcpy(&addr, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);
				const char * paddr = (const char *)addr;

				// If we have sampled an address in our watch range, and it does
				// not belong to the stack, then proceed...
				if((paddr >= watchStartByte && paddr <= watchEndByte) &&
						!(paddr >= stackStart && paddr <= stackEnd)) {
					numHits++;

					// Calculate the offset of this
					// byte from the start of the heap.
					long long access_byte_offset = paddr - watchStartByte;

					if(debug) {
						printf("access_offset=%lld, watchStartByte=%p, "
							"watchEndByte=%p, stackStart=%p, stackEnd=%p\n",
							access_byte_offset, watchStartByte, watchEndByte,
							stackStart, stackEnd);
					}
					if((access_byte_offset >= 0) &&
							(access_byte_offset < SHADOW_MEM_SIZE)) {
						// Calculate which word we're in
						long long access_word_offset =
							access_byte_offset / WORD_SIZE;

						// Check to see if we have sampled the address of an
						// object's header rather than its body. Increment the
						// corresponding shadow word's value only if this is
						// NOT an object header.
						long *current_value =
							(long *)shadow_mem + access_word_offset;
						if(!isWordMallocHeader(current_value)) {
							if(debug) {
								printf("attempting to increment %p/%p in "
									"[%p,%p), offset=%lld, current value=%ld\n",
									paddr, current_value, shadow_mem,
									(shadow_mem + SHADOW_MEM_SIZE),
									access_byte_offset, *current_value);
							}
							(*current_value)++;

							/*
							// DEBUG BLOCK, USED TO DETECT ORPHAN SAMPLE HITS
							char * accessShadowByte = (char *)shadow_mem + access_byte_offset;
							current_value--;
							while(current_value > (long *)shadow_mem) {
								if(isWordMallocHeader(current_value)) {
									long current_byte_offset = (char *)current_value - (char *)shadow_mem;
									long callsite_id = *current_value;
									long * realObjHeader =
										(long *)((char *)watchStartByte + current_byte_offset);
									long objSizeInBytes = *realObjHeader - 1;
									char * highObjectBoundary = (char *)current_value + objSizeInBytes;

									if(debug && (accessShadowByte > highObjectBoundary)) {
										fprintf(output, "sampled byte %p/%p does not belong to the nearest "
											"object @ %p + %ld(0x%lx)\n", paddr, accessShadowByte,
											current_value, objSizeInBytes, objSizeInBytes);
										fprintf(output, "callsite_id = 0x%lx, ip = 0x%lx\n\n", callsite_id,
											ip);
									}
									break;
								} else {
									if(*current_value != 0) {
										fprintf(output, "! no malloc header @ %p, value = 0x%lx\n",
											current_value, *current_value);
									}
								}
								current_value--;
							}
							// END OF DEBUG BLOCK
							*/
						} else {
							// Decrement the hit counter if what we have sampled
							// is actually a malloc header.
							numHits--;
						}
					}
				} else {
					//printf("watch area miss: watchStart=%p, paddr=%p, watchEnd=%p\n",
					//	watchStartByte, paddr, watchEndByte);
				}
			}

			if(sample_type & PERF_SAMPLE_ID) {
				uint64_t sample_id;
				memcpy(&sample_id, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_STREAM_ID) {
				uint64_t sample_stream_id;
				memcpy(&sample_stream_id, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_CPU) {
				uint32_t cpu, res;
				memcpy(&cpu, &data[offset], sizeof(uint32_t));
				memcpy(&res, &data[(offset + sizeof(uint32_t))],
					sizeof(uint32_t));
				offset += 2 * sizeof(uint32_t);
			}

			if(sample_type & PERF_SAMPLE_PERIOD) {
				uint64_t period;
				memcpy(&period, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_CALLCHAIN) {
				uint64_t nr, ip;
				memcpy(&nr, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);

				for(i = 0; i < nr; i++) {
					memcpy(&ip, &data[offset], sizeof(uint64_t));
					offset += sizeof(uint64_t);
				}
			}

			if(sample_type & PERF_SAMPLE_RAW) {
				uint32_t size;
				memcpy(&size, &data[offset], sizeof(uint32_t));
				offset += sizeof(uint32_t);
			}

			if(sample_type & PERF_SAMPLE_WEIGHT) {
				long long weight;
				memcpy(&weight,&data[offset],sizeof(long long));
				offset+=8;
			}

			if(sample_type & PERF_SAMPLE_DATA_SRC) {
				uint64_t src;
				memcpy(&src, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);

				/*
				// DEBUG BLOCK, USED TO PRINT LOAD/STORE STATUS AS WELL AS
				// PEAK AT THE FIRST EIGHT BYTES OF DATA AT THE SAMPLED
				// MEMORY ADDRESS.
				printf("> sampled address @ %p", paddr);
				if(src & PERF_MEM_OP_LOAD)
					printf(" [Load]");
				if(src & PERF_MEM_OP_STORE)
					printf(" [Store]");
				printf("\n");
				if(paddr)
					printf("\t data = 0x%lx\n", *((long *)paddr));
				// END DEBUG BLOCK
				*/
			}

			if(sample_type & PERF_SAMPLE_IDENTIFIER) {
				long long abi;
				memcpy(&abi,&data[offset],sizeof(long long));
				offset+=8;
			}
			break;
		} // end case block
		} // end switch statement
		// Move the offset counter ahead by the size given in the event header.
		offset = starting_offset + event->size;

		numSamples++;
	}

	// Tell perf where we left off reading; this prevents
	// perf from overwriting data we have not read yet.
	control_page->data_tail = head;

	return head;
}

void sampleHandler(int signum, siginfo_t *info, void *p) {
	numSignals++;

	// If the overflow counter has reached zero (indicated by the POLL_HUP code),
	// read the sample data and reset the overflow counter to start again.
	if(info->si_code == POLL_HUP) {
		doSampleRead();
		ioctl(perf_fd, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL);
		ioctl(perf_fd2, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL);
	}
}

void setupSampling(void) {
	struct perf_event_attr pe_load, pe_store;
	memset(&pe_load, 0, sizeof(struct perf_event_attr));

	pe_load.type = PERF_TYPE_RAW;
	pe_load.size = sizeof(struct perf_event_attr);

	// config: this field is set depending on the type of bridge in the processor 
	// and whether we would like to sample load/store accesses.
	// For more information see the Intel 64 and IA-32 Architectures Software Developer's Manual:
	// http://courses.cs.washington.edu/courses/cse451/15au/readings/ia32-3.pdf
	pe_load.config = LOAD_ACCESS;
	//pe_load.config = STORE_ACCESS;

	//Sample_period/freq: Setting the rate of recording. 
	//For perf, it generates an overflow and start writing to mmap buffer in a fixed frequency set here.
	//pe_load.sample_period = 1000;
	pe_load.sample_freq = 50000;

	//Set this field to use frequency instead of period, see above.
	pe_load.freq = 1;

	//Sample_type: Specifies which should be sampled in an access. 
	pe_load.sample_type = sample_type;

	//This field specifies the format of the data returned by
	//read() on a perf_event_open() file descriptor. 
	pe_load.read_format = read_format;

	//Disabled: whether the counter starts with disabled/enabled status. 
	pe_load.disabled = 1;

	//Pinned: The pinned bit specifies that the counter should always be on
	//the CPU if at all possible.
	pe_load.pinned = 0;

	//Inherit:The inherit bit specifies that this counter should count
	//events of child tasks as well as the task specified.
	//WARNING: It is this property that breaks perf_event_open(&pe, 0, -1...)
	//UPDATE: this is because it does not work with the PERF_FORMAT_GROUP
	//read format flag.
	pe_load.inherit = 0;

	//Exclucive:The exclusive bit specifies that when this counter's group is
	//on the CPU, it should be the only group using the CPU's counters.
	pe_load.exclusive = 0;

	//Exclude_xxx: Do not sample a specified side of events, 
	//user, kernel, or hypevisor
	pe_load.exclude_user = 0;
	pe_load.exclude_kernel = 1;
	pe_load.exclude_hv = 1;

	//Exclude_idle: If set, don't count when the CPU is idle.
	pe_load.exclude_idle = 1;

	//Mmap: The mmap bit enables generation of PERF_RECORD_MMAP samples
	//for every mmap() call that has PROT_EXEC set.
	pe_load.mmap = 0;

	//Comm: The comm bit enables tracking of process command name as
	//modified by the exec() and prctl(PR_SET_NAME) system calls as
	//well as writing to /proc/self/comm.
	//If comm_exec is also set, then the misc flag
	//PERF_RECORD_MISC_COMM_EXEC can be used to
	//differentiate the exec() case from the others.
	// FIXME
	pe_load.comm = 0;

	//Inherit_stat: This bit enables saving of event counts on context switch for
	//inherited tasks. So it is only useful when 'inherit' set to 1.
	pe_load.inherit_stat = 0;

	//When 'disable' and this bit is set, a counter is automatically enabled after a call to exec().
	pe_load.enable_on_exec = 1;

	//Task: If this bit is set, then fork/exit notifications are included in the ring buffer.
	pe_load.task = 0;

	//Watermark: If set, have an overflow notification happen when we cross the
	//wakeup_watermark boundary.
	pe_load.watermark = 0;

	//Precise_ip: This controls the amount of skid. See perf_event.h
	pe_load.precise_ip = 2;

	//Mmap_data: This enables generation of PERF_RECORD_MMAP samples for mmap() calls 
	//that do not have PROT_EXEC set
	//pe_load.mmap_data = 1;
	pe_load.mmap_data = 0;

	//Sample_id_all: TID, TIME, ID, STREAM_ID, and CPU added to every sample.
	pe_load.sample_id_all = 0;

	//Exclude_xxx: Sample guest/host instances or not.
	pe_load.exclude_host = 0;
	pe_load.exclude_guest = 1;

	//Exclude_callchain_xxx: exclude callchains from kernel/user.
	pe_load.exclude_callchain_kernel = 1; // exclude kernel callchains
	pe_load.exclude_callchain_user = 1; // exclude user callchains

	//Mmap2:Generate an extended executable mmap record that contains
	//enough additional information to uniquely identify shared
	//mappings. Set 'mmap' before setting this.
	//pe_load.mmap2 = 1;
	pe_load.mmap2 = 0;

	pe_load.comm_exec = 0; // flag comm events that are due to an exec

	//wakeup_xxx: This union sets how many samples (wakeup_events) or bytes
	//(wakeup_watermark) happen before an overflow notification happens.
	pe_load.wakeup_events = 0;
	pe_load.wakeup_watermark = 0;

	//bp_type: This chooses the breakpoint type_load.
	//pe_load.bp_type = HW_BREAKPOINT_EMPTY;

	//bp_addr: For write/read samples, it collects memory address.
	//pe_load.bp_addr = 0x3;

	//Expanding config. See 'config'.
	//pe_load.config1 = 0x3;
	//pe_load.config2 = 0;

	//bp_len: length of the breakpoint being measured if type
	//is PERF_TYPE_BREAKPOINT.
	pe_load.bp_len = 0;

	//branch_sample_type: If PERF_SAMPLE_BRANCH_STACK is enabled, then this specifies
	//what branches to include in the branch record.
	pe_load.branch_sample_type = 0;

	//sample_regs_user:  This bit mask defines the set of user 
	//CPU registers to dump on samples.
	pe_load.sample_regs_user = 0;

	//sample_stack_user: This defines the size of the user stack to dump if
	//PERF_SAMPLE_STACK_USER is specified.
	pe_load.sample_stack_user = 0;

	// Make an exact copy of the pe_load attributes to be used for the
	// corresponding store events' attributes.
	memcpy(&pe_store, &pe_load, sizeof(struct perf_event_attr));
	pe_store.config = STORE_ACCESS;

	// Create the perf_event for this thread on all CPUs with no event group
	// Second parameter (target thread): 0=self, -1=cpu-wide mode
	// Third parameter: cpu: -1 == any
	// Fourth parameter: group id (-1 for group leader)
	perf_fd = perf_event_open(&pe_load, 0, -1, -1, 0);
	if(perf_fd == -1) {
		fprintf(stderr, "Failed to open perf event for pe_load: %s\n", strerror(errno));
		abort();
	}

	// Setting up 9 pages to pass information about a trap
	if((our_mmap = mmap(NULL, MAPSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, perf_fd, 0)) == MAP_FAILED) {
		fprintf(stderr, "ERROR: mmap failed on perf_fd=%d: %s\n", perf_fd, strerror(errno));
		abort();
	}

	// Set the perf_event file to async mode
	if(fcntl(perf_fd, F_SETFL, O_RDWR|O_NONBLOCK|O_ASYNC) == -1) {
		fprintf(stderr, "Failed to set perf event file to ASYNC mode: %s\n", strerror(errno));
		abort();
	}

	perf_fd2 = perf_event_open(&pe_store, 0, -1, -1, 0);
	if(perf_fd2 == -1) {
		fprintf(stderr, "Failed to open perf event for pe_store: %s\n", strerror(errno));
		abort();
	}
	if(ioctl(perf_fd2, PERF_EVENT_IOC_SET_OUTPUT, perf_fd) == -1) {
		fprintf(stderr, "Call to ioctl failed on line %d: %s\n", __LINE__, strerror(errno));
		abort();
	}

	// Deliver the signal to this thread
	struct f_owner_ex owner = {F_OWNER_TID, tid};
	if(fcntl(perf_fd, F_SETOWN_EX, &owner) == -1) {
		fprintf(stderr, "Failed to set the owner of the perf event file: %s\n", strerror(errno));
		abort();
	}
	if(fcntl(perf_fd2, F_SETOWN_EX, &owner) == -1) {
		fprintf(stderr, "Failed to set the owner of the perf event file: %s\n", strerror(errno));
		abort();
	}

	// Tell the file to send a SIGIO when an event occurs
	if(fcntl(perf_fd, F_SETSIG, SIGIO) == -1) {
		fprintf(stderr, "Failed to set perf event file's async signal: %s\n", strerror(errno));
		abort();
	}
	if(fcntl(perf_fd2, F_SETSIG, SIGIO) == -1) {
		fprintf(stderr, "Failed to set perf event file's async signal: %s\n", strerror(errno));
		abort();
	}

	startSampling();
}

int64_t get_trace_count(int fd) {
	int64_t count;
	read(fd, &count, sizeof(int64_t));
	return count;
}

int initSampling(void) {
	tid = gettid();

	struct sigaction sa;
	sigset_t set;

	sigemptyset(&set);
	sa.sa_sigaction = sampleHandler;
	sa.sa_flags = SA_SIGINFO;
	sa.sa_mask = set;

	if(sigaction(SIGIO, &sa, NULL) != 0) {
		fprintf(stderr, "Failed to set SIGIO handler: %s\n", strerror(errno));
		abort();
	}

	data = (unsigned char *) malloc(DATA_MAPSIZE);
	if(data == NULL) { return -1; }

	// Setup the sampling
	setupSampling();

	return 0;
}
