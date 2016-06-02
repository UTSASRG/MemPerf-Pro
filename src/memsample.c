#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <asm/unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "memsample.h"

// Number of MMAP pages needs to be in the form 2^N + 1.
#define MMAP_PAGES 5
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * getpagesize())
#define DATA_MAPSIZE (DATA_MMAP_PAGES * getpagesize())
#define OVERFLOW_INTERVAL 20
#define LOAD_ACCESS 0x1cd
#define STORE_ACCESS 0x2cd

int64_t get_trace_count(int fd);

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
	return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

pid_t gettid() {
	return syscall(__NR_gettid);
}

extern void * shadow_mem;

__thread int perf_fd, perf_fd2;
__thread void *our_mmap;
__thread long long prev_head;
__thread unsigned char *data;
__thread pid_t tid;
__thread char output_buffer[512];

int sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR |
				PERF_SAMPLE_CPU | PERF_SAMPLE_CALLCHAIN | PERF_SAMPLE_DATA_SRC | PERF_SAMPLE_WEIGHT;


int read_format = PERF_FORMAT_GROUP;
//                 PERF_FORMAT_ID |
//                 PERF_FORMAT_TOTAL_TIME_ENABLED |
//                 PERF_FORMAT_TOTAL_TIME_RUNNING;

void startSampling() {
	// Start the event
	/*
	if(ioctl(perf_fd, PERF_EVENT_IOC_RESET, 0) == -1) {
		fprintf(stderr, "Failed to reset perf event w/ fd %d: %s\n", perf_fd, strerror(errno));
		abort();
	}
	*/
	if(ioctl(perf_fd, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL) == -1) {
		fprintf(stderr, "Failed to refresh perf event w/ fd %d, line %d: %s\n", perf_fd, __LINE__, strerror(errno));
		abort();
	}
	if(ioctl(perf_fd2, PERF_EVENT_IOC_REFRESH, OVERFLOW_INTERVAL) == -1) {
		fprintf(stderr, "Failed to refresh perf event w/ fd %d, line %d: %s\n", perf_fd2, __LINE__, strerror(errno));
		abort();
	}
	if(ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
		fprintf(stderr, "Failed to enable perf event w/ fd %d, line %d: %s\n", perf_fd, __LINE__, strerror(errno));
		abort();
	}
}

void stopSampling() {
	if(ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0) == -1) {
		fprintf(stderr, "Failed to disable perf event: %s\n", strerror(errno));
		abort();
	}
}

static int handle_struct_read_format(unsigned char *sample, int read_format,
															void *validation) {
	int i;
	int offset = 0;

	if (read_format & PERF_FORMAT_GROUP) {
		long long nr,time_enabled,time_running;

		memcpy(&nr,&sample[offset],sizeof(long long));
		offset+=8;

		if(read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
			memcpy(&time_enabled,&sample[offset],sizeof(long long));
			offset+=8;
		}
		if(read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
			memcpy(&time_running,&sample[offset],sizeof(long long));
			offset+=8;
		}

		for(i=0;i<nr;i++) {
			long long value, id;

			memcpy(&value,&sample[offset],sizeof(long long));
			offset+=8;

			if(read_format & PERF_FORMAT_ID) {
				memcpy(&id,&sample[offset],sizeof(long long));
				offset+=8;
			}
		}
	} else {
		long long value,time_enabled,time_running,id;

		memcpy(&value,&sample[offset],sizeof(long long));
		offset+=8;

		if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
			memcpy(&time_enabled,&sample[offset],sizeof(long long));
			offset+=8;
		}
		if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
			memcpy(&time_running,&sample[offset],sizeof(long long));
			offset+=8;
		}
		if (read_format & PERF_FORMAT_ID) {
			memcpy(&id,&sample[offset],sizeof(long long));
			offset+=8;

		}
	}

	return offset;
}

long long perf_mmap_read(long long prev_head, long long reg_mask, void *validate) {
	struct perf_event_header *event;
	struct perf_event_mmap_page *control_page = (struct perf_event_mmap_page *) our_mmap;
	long long head, offset;
	int i, size;
	long long copy_amt, prev_head_wrap;
	void *data_mmap = (void *)((size_t)our_mmap + getpagesize());

	if(control_page == NULL) {
		fprintf(stderr, "ERROR: control_page=%p, our_mmap=%p, data_mmap=%p\n", control_page, our_mmap, data_mmap);
		raise(SIGUSR1);
		return -1;
	}

	head = control_page->data_head;
	size = head - prev_head;

	if(head < prev_head)
		prev_head = 0;

	if(size > DATA_MAPSIZE)
		fprintf(stderr, "Error! we overflowed the mmap buffer with %d>%d bytes\n", size, DATA_MAPSIZE);

	prev_head_wrap = prev_head % DATA_MAPSIZE;

	copy_amt = size;

	// If we have to wrap around to the beginning of the ring buffer in order
	// to copy all of the new data, start by copying from prev_head_wrap
	// to the end of the buffer.
	if(size > (DATA_MAPSIZE - prev_head_wrap))
		copy_amt = DATA_MAPSIZE - prev_head_wrap;

	memcpy(data, (unsigned char *)data_mmap + prev_head_wrap, copy_amt);

	if(size > (DATA_MAPSIZE - prev_head_wrap)) {
		memcpy(data + (DATA_MAPSIZE - prev_head_wrap), (unsigned char *)data_mmap,
					(size - copy_amt));
	}

	offset = 0;
	while(offset < size) {
		long long starting_offset = offset;

		event = (struct perf_event_header *) &data[offset];

		// skip over the header we just read from above
		offset += sizeof(struct perf_event_header); 

		/***********************/
		/* Print event Details */
		/***********************/
		//int lenDetails;
		//int quiet = 0;
		//int debug = 1;
		switch(event->type) {
		/*
		// Printing output version below, commented out:
		case PERF_RECORD_SAMPLE: {
            if(sample_type & PERF_SAMPLE_IP) {
                uint64_t ip;
                memcpy(&ip, &data[offset], sizeof(uint64_t));

                snprintf(output_buffer, 512, "\tPERF_SAMPLE_IP, IP: %lx\n", ip);
                lenDetails = strlen(output_buffer);
                if(!quiet)
                    write(STDOUT_FILENO, output_buffer, lenDetails);

                offset += sizeof(uint64_t);
            }

            if(sample_type & PERF_SAMPLE_TID) {
                uint32_t pid, tid;
                memcpy(&pid, &data[offset], sizeof(uint32_t));
                memcpy(&tid, &data[(offset + sizeof(uint32_t))], sizeof(uint32_t));

                snprintf(output_buffer, 512, "\tPERF_SAMPLE_TID, pid: %d, tid: %d\n", pid, tid);
                lenDetails = strlen(output_buffer);
                if(!quiet)
                    write(STDOUT_FILENO, output_buffer, lenDetails);

                offset += 2 * sizeof(uint32_t);
            }

            if(sample_type & PERF_SAMPLE_TIME) {
                uint64_t time;
                memcpy(&time, &data[offset], sizeof(uint64_t));

                snprintf(output_buffer, 512, "\tPERF_SAMPLE_TIME, time: %lu\n", time);
                lenDetails = strlen(output_buffer);
                if(!quiet)
                    write(STDOUT_FILENO, output_buffer, lenDetails);

                offset += sizeof(uint64_t);
            }

            if(sample_type & PERF_SAMPLE_ADDR) {
                uint64_t addr;
                memcpy(&addr, &data[offset], sizeof(uint64_t));

                snprintf(output_buffer, 512, "\tPERF_SAMPLE_ADDR, addr: %lx\n", addr);
                lenDetails = strlen(output_buffer);
                if(!quiet)
                    write(STDOUT_FILENO, output_buffer, lenDetails);

                offset += sizeof(uint64_t);

                // debug code:
                if(addr) {
                    unsigned long *word = (unsigned long *) addr;
                    snprintf(output_buffer, 512, "\tmemory at this addr =\n");
                    lenDetails = strlen(output_buffer);
                    if(!quiet)
                        write(STDOUT_FILENO, output_buffer, lenDetails);
                    int k;
                    for(k = 0; k < 4; k++, word++) {
                        snprintf(output_buffer, 512, "\t\t%p: %lx\n", word, *word);
                        lenDetails = strlen(output_buffer);
                        if(!quiet)
                            write(STDOUT_FILENO, output_buffer, lenDetails);
                    }
                }
            }

            if(sample_type & PERF_SAMPLE_ID) {
                uint64_t sample_id;
                memcpy(&sample_id, &data[offset], sizeof(uint64_t));

                snprintf(output_buffer, 512, "\tPERF_SAMPLE_ID, sample_id: %lu. Offset %llx\n", sample_id, offset);
                lenDetails = strlen(output_buffer);
                if(!quiet)
                    write(STDOUT_FILENO, output_buffer, lenDetails);

                offset += sizeof(uint64_t);
            }

            if(sample_type & PERF_SAMPLE_STREAM_ID) {
                uint64_t sample_stream_id;
                memcpy(&sample_stream_id, &data[offset], sizeof(uint64_t));
                if(!quiet) printf("\tPERF_SAMPLE_STREAM_ID, sample_stream_id: %lu\n", sample_stream_id);
                offset += sizeof(uint64_t);
            }

            if(sample_type & PERF_SAMPLE_CPU) {
                uint32_t cpu, res;
                memcpy(&cpu, &data[offset], sizeof(uint32_t));
                memcpy(&res, &data[(offset + sizeof(uint32_t))], sizeof(uint32_t));
                snprintf(output_buffer, 512, "\tPERF_SAMPLE_CPU, cpu: %d, res: %d\n", cpu, res);
                lenDetails = strlen(output_buffer);
                if(!quiet)
                    write(STDOUT_FILENO, output_buffer, lenDetails);

                offset += 2 * sizeof(uint32_t);
            }

            if(sample_type & PERF_SAMPLE_PERIOD) {
                uint64_t period;
                memcpy(&period, &data[offset], sizeof(uint64_t));
                if(!quiet) printf("\tPERF_SAMPLE_PERIOD, period: %lu\n", period);
                offset += sizeof(uint64_t);
            }

            if(sample_type & PERF_SAMPLE_READ) {
                int length;
                if(!quiet) printf("\tPERF_SAMPLE_READ, read_format\n");
                length = handle_struct_read_format(&data[offset],
                                           read_format, validate);
                                // todo: if handle_struct_read_format() gets fixed, use this line instead.
                //if(length >= 0) { offset += length; }
            }

            if(sample_type & PERF_SAMPLE_CALLCHAIN) {
                uint64_t nr, ip;
                memcpy(&nr, &data[offset], sizeof(uint64_t));
                offset += sizeof(uint64_t);
                if(!quiet) printf("\tPERF_SAMPLE_CALLCHAIN, callchain length: %lu\n", nr);

                for(i = 0; i < nr; i++) {
                    memcpy(&ip, &data[offset], sizeof(uint64_t));
                    if (!quiet) printf("\t\t ip[%d]: %lx\n", i, ip);
                    offset += sizeof(uint64_t);
                }
            }

            if(sample_type & PERF_SAMPLE_RAW) {
                uint32_t size;
                memcpy(&size, &data[offset], sizeof(uint32_t));
                offset += sizeof(uint32_t);
                if(debug) {
                    printf("\tPERF_SAMPLE_RAW, Raw length: %d\n", size);
                    printf("\t\t");
                    for(i = 0; i < size; i++) {
                        printf("%d ", data[offset]);
                        offset += sizeof(char);
                    }
                    printf("\n");
                }
            }
		*/


		// Sample data
		case PERF_RECORD_SAMPLE: {
 			if(sample_type & PERF_SAMPLE_IP) {
				uint64_t ip;
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
				memcpy(&res, &data[(offset + sizeof(uint32_t))], sizeof(uint32_t));
				offset += 2 * sizeof(uint32_t);
			}

			if(sample_type & PERF_SAMPLE_PERIOD) {
				uint64_t period;
				memcpy(&period, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);
			}

			if(sample_type & PERF_SAMPLE_READ) {
				int length;
				length = handle_struct_read_format(&data[offset], read_format, validate);
				if(length >= 0) { offset += length; }
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

			if(sample_type & PERF_SAMPLE_BRANCH_STACK) {
				long long bnr;
				memcpy(&bnr,&data[offset],sizeof(long long));
				offset+=8;

				for(i=0;i<bnr;i++) {
					long long from,to,flags;

					/* From value */
					memcpy(&from,&data[offset],sizeof(long long));
					offset+=8;


					/* To Value */
					memcpy(&to,&data[offset],sizeof(long long));
					offset+=8;

					/* Flags */
					memcpy(&flags,&data[offset],sizeof(long long));
					offset+=8;
	   		}
			}

			if (sample_type & PERF_SAMPLE_STACK_USER) {
				long long size,dyn_size;
				int *stack_data;

				memcpy(&size,&data[offset],sizeof(long long));
				offset+=8;

				stack_data=(int *)malloc(size);
				memcpy(stack_data,&data[offset],size);
				offset+=size;

				memcpy(&dyn_size,&data[offset],sizeof(long long));
				offset+=8;

				free(stack_data);
			}

			if (sample_type & PERF_SAMPLE_WEIGHT) {
				long long weight;

				memcpy(&weight,&data[offset],sizeof(long long));
				offset+=8;
			}

			if (sample_type & PERF_SAMPLE_DATA_SRC) {
				uint64_t src;

				memcpy(&src, &data[offset], sizeof(uint64_t));
				offset += sizeof(uint64_t);
			}


			/*
			// Printed output version, commented out.
            if (sample_type & PERF_SAMPLE_DATA_SRC) {
                uint64_t src;

                memcpy(&src, &data[offset], sizeof(uint64_t));
                if(debug) printf("\tPERF_SAMPLE_DATA_SRC, Raw: %lx\n", src);
                offset += sizeof(uint64_t);

                if(debug) {
                    if (src!=0) printf("\t\t");
                    if (src & (PERF_MEM_OP_NA))
                        printf("Op Not available ");
                    if (src & (PERF_MEM_OP_LOAD))
                        printf("Load ");
                    if (src & (PERF_MEM_OP_STORE))
                        printf("Store ");
                    if (src & (PERF_MEM_OP_PFETCH))
                        printf("Prefetch ");
                    if (src & (PERF_MEM_OP_EXEC))
                        printf("Executable code ");
                    if (src & (PERF_MEM_LVL_NA<<PERF_MEM_LVL_SHIFT))
                        printf("Level Not available ");
                    if (src & (PERF_MEM_LVL_HIT<<PERF_MEM_LVL_SHIFT))
                        printf("Hit ");
                    if (src & (PERF_MEM_LVL_MISS<<PERF_MEM_LVL_SHIFT))
                        printf("Miss ");
                    if (src & (PERF_MEM_LVL_L1<<PERF_MEM_LVL_SHIFT))
                        printf("L1 cache ");
                    if (src & (PERF_MEM_LVL_LFB<<PERF_MEM_LVL_SHIFT))
                        printf("Line fill buffer ");
                    if (src & (PERF_MEM_LVL_L2<<PERF_MEM_LVL_SHIFT))
                        printf("L2 cache ");
                    if (src & (PERF_MEM_LVL_L3<<PERF_MEM_LVL_SHIFT))
                        printf("L3 cache ");
                    if (src & (PERF_MEM_LVL_LOC_RAM<<PERF_MEM_LVL_SHIFT))
                        printf("Local DRAM ");
                    if (src & (PERF_MEM_LVL_REM_RAM1<<PERF_MEM_LVL_SHIFT))
                        printf("Remote DRAM 1 hop ");
                    if (src & (PERF_MEM_LVL_REM_RAM2<<PERF_MEM_LVL_SHIFT))
                        printf("Remote DRAM 2 hops ");
                    if (src & (PERF_MEM_LVL_REM_CCE1<<PERF_MEM_LVL_SHIFT))
                        printf("Remote cache 1 hop ");
                    if (src & (PERF_MEM_LVL_REM_CCE2<<PERF_MEM_LVL_SHIFT))
                        printf("Remote cache 2 hops ");
                    if (src & (PERF_MEM_LVL_IO<<PERF_MEM_LVL_SHIFT))
                        printf("I/O memory ");
                    if (src & (PERF_MEM_LVL_UNC<<PERF_MEM_LVL_SHIFT))
                        printf("Uncached memory ");

                    if (src & (PERF_MEM_SNOOP_NA<<PERF_MEM_SNOOP_SHIFT))
                        printf("Not available ");
                    if (src & (PERF_MEM_SNOOP_NONE<<PERF_MEM_SNOOP_SHIFT))
                        printf("No snoop ");
                    if (src & (PERF_MEM_SNOOP_HIT<<PERF_MEM_SNOOP_SHIFT))
                        printf("Snoop hit ");
                    if (src & (PERF_MEM_SNOOP_MISS<<PERF_MEM_SNOOP_SHIFT))
                        printf("Snoop miss ");
                    if (src & (PERF_MEM_SNOOP_HITM<<PERF_MEM_SNOOP_SHIFT))
                        printf("Snoop hit modified ");

                    if (src & (PERF_MEM_LOCK_NA<<PERF_MEM_LOCK_SHIFT))
                        printf("Not available ");
                    if (src & (PERF_MEM_LOCK_LOCKED<<PERF_MEM_LOCK_SHIFT))
                        printf("Locked transaction ");

                    if (src & (PERF_MEM_TLB_NA<<PERF_MEM_TLB_SHIFT))
                        printf("Not available ");
                    if (src & (PERF_MEM_TLB_HIT<<PERF_MEM_TLB_SHIFT))
                        printf("Hit ");
                    if (src & (PERF_MEM_TLB_MISS<<PERF_MEM_TLB_SHIFT))
                        printf("Miss ");
                    if (src & (PERF_MEM_TLB_L1<<PERF_MEM_TLB_SHIFT))
                        printf("Level 1 TLB ");
                    if (src & (PERF_MEM_TLB_L2<<PERF_MEM_TLB_SHIFT))
                        printf("Level 2 TLB ");
                    if (src & (PERF_MEM_TLB_WK<<PERF_MEM_TLB_SHIFT))
                        printf("Hardware walker ");
                    if (src & ((long)PERF_MEM_TLB_OS<<PERF_MEM_TLB_SHIFT))
                        printf("OS fault handler ");
                }
			}
			*/


			if (sample_type & PERF_SAMPLE_IDENTIFIER) {
				long long abi;

				memcpy(&abi,&data[offset],sizeof(long long));
				offset+=8;
			}

			if (sample_type & PERF_SAMPLE_TRANSACTION) {
				long long abi;

				memcpy(&abi,&data[offset],sizeof(long long));
				offset+=8;
			}
		}
		break;
		}
		// Move the offset counter ahead by the size given in the event header.
		offset = starting_offset + event->size;
	}

	// Tell perf where we left off reading; this prevents
	// perf from overwriting data we have not read yet.
	control_page->data_tail = head;

	return head;
}

void sampleHandler(int signum, siginfo_t *info, void *p) {
	// If the overflow counter has reached zero (indicated by the POLL_HUP code),
	// read the sample data and reset the overflow counter to start again.
	if(info->si_code == POLL_HUP) {
		prev_head = perf_mmap_read(prev_head, 0, NULL);
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
	pe_load.sample_freq = 4000;

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

	//Set this field to use frequency instead of period, see above.
	pe_load.freq = 1;

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
	pe_load.exclude_guest = 0;

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

	memcpy(&pe_store, &pe_load, sizeof(struct perf_event_attr));
	pe_store.disabled = 1;
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

	// Tell the file to send a SIGIO when an event occurs
	if(fcntl(perf_fd, F_SETSIG, SIGIO) == -1) {
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
