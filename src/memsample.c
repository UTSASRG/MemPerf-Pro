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
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/mman.h>
#include <asm/perf_regs.h>
#include <pthread.h>
#include "memsample.h"

// Number of MMAP pages needs to be in the form 2^N + 1.
#define MMAP_PAGES 9
#define DATA_MMAP_PAGES (MMAP_PAGES - 1)
#define MAPSIZE (MMAP_PAGES * getpagesize())
#define DATA_MAPSIZE (DATA_MMAP_PAGES * getpagesize())

int64_t get_trace_count(int fd);

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

pid_t gettid() {
  return syscall(__NR_gettid);
}

__thread int perf_fd;
__thread void *our_mmap;
__thread long long prev_head;
__thread unsigned char *data;
__thread pid_t tid;
__thread char output_buffer[512];

//int sample_type = 0xc10f;
int sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR |
			 							PERF_SAMPLE_CPU | PERF_SAMPLE_CALLCHAIN | PERF_SAMPLE_DATA_SRC | PERF_SAMPLE_WEIGHT;


int read_format = PERF_FORMAT_GROUP;
//                 PERF_FORMAT_ID |
//                 PERF_FORMAT_TOTAL_TIME_ENABLED |
//                 PERF_FORMAT_TOTAL_TIME_RUNNING;

void enable_trace() {
  // Start the event
  if(ioctl(perf_fd, PERF_EVENT_IOC_RESET, 0) == -1) {
    fprintf(stderr, "Failed to reset perf event w/ fd %d: %s\n", perf_fd, strerror(errno));
    abort();
  }
  if(ioctl(perf_fd, PERF_EVENT_IOC_REFRESH, 1) == -1) {
    fprintf(stderr, "Failed to refresh perf event w/ fd %d: %s\n", perf_fd, strerror(errno));
    abort();
  }
}

void disable_trace() {
  if(ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0) == -1) {
    fprintf(stderr, "Failed to disable perf event: %s\n", strerror(errno));
    abort();
  }
}

void startSampling() {
	enable_trace();
}

void stopSampling() {
	disable_trace();
}

static int handle_struct_read_format(unsigned char *sample,
             int read_format, void *validation) {
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

		/********************/
		/* Print event Type */
		/********************/

		// skip over the header we just read from above
		offset += sizeof(struct perf_event_header); 

		/***********************/
		/* Print event Details */
		/***********************/
		switch(event->type) {

		/* Lost */
		case PERF_RECORD_LOST: {
			long long id,lost;
			memcpy(&id,&data[offset],sizeof(long long));
			offset+=8;
			memcpy(&lost,&data[offset],sizeof(long long));
			offset+=8;
			}
			break;

		/* COMM */
		case PERF_RECORD_COMM: {
			int pid,tid,string_size;
			char *string;

			memcpy(&pid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			offset+=4;

			/* FIXME: sample_id handling? */

			/* two ints plus the 64-bit header */
			string_size=event->size-16;
			string=(char *)calloc(string_size,sizeof(char));
			memcpy(string,&data[offset],string_size);
			offset+=string_size;
			if(string) free(string);
			}
			break;

		/* Fork */
		case PERF_RECORD_FORK: {
			int pid,ppid,tid,ptid;
			long long fork_time;

			memcpy(&pid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&ppid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&ptid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&fork_time,&data[offset],sizeof(long long));
			offset+=8;
			}
			break;

		/* mmap */
		case PERF_RECORD_MMAP: {
			int pid,tid,string_size;
			long long address,len,pgoff;
			char *filename;

			memcpy(&pid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&address,&data[offset],sizeof(long long));
			offset+=8;
			memcpy(&len,&data[offset],sizeof(long long));
			offset+=8;
			memcpy(&pgoff,&data[offset],sizeof(long long));
			offset+=8;

			string_size=event->size-40;
			filename=(char *)calloc(string_size,sizeof(char));
			memcpy(filename,&data[offset],string_size);
			offset+=string_size;
			if (filename) free(filename);

			}
			break;

		/* mmap2 */
		case PERF_RECORD_MMAP2: {
			int pid,tid,string_size;
			long long address,len,pgoff;
			int major,minor;
			long long ino,ino_generation;
			int prot,flags;
			char *filename;

			memcpy(&pid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&address,&data[offset],sizeof(long long));
			offset+=8;
			memcpy(&len,&data[offset],sizeof(long long));
			offset+=8;
			memcpy(&pgoff,&data[offset],sizeof(long long));
			offset+=8;
			memcpy(&major,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&minor,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&ino,&data[offset],sizeof(long long));
			offset+=8;
			memcpy(&ino_generation,&data[offset],sizeof(long long));
			offset+=8;
			memcpy(&prot,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&flags,&data[offset],sizeof(int));
			offset+=4;

			string_size=event->size-72;
			filename=(char *)calloc(string_size,sizeof(char));
			memcpy(filename,&data[offset],string_size);
			offset+=string_size;
			if (filename) free(filename);

			}
			break;

		/* Exit */
		case PERF_RECORD_EXIT: {
			int pid,ppid,tid,ptid;
			long long fork_time;

			memcpy(&pid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&ppid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&tid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&ptid,&data[offset],sizeof(int));
			offset+=4;
			memcpy(&fork_time,&data[offset],sizeof(long long));
			offset+=8;
			}
			break;

		/* Sample */
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
	if(info->si_code != POLL_HUP) {
		if(info->si_code == POLL_IN)
			fprintf(stderr, "ERROR: signal code was POLL_IN on tid=%d\n", tid);
		fprintf(stderr, "ERROR: signal was not equal to POLL_HUP on pid/tid=%d/%d\n", getpid(), tid);
		exit(EXIT_FAILURE);
	}
	prev_head = perf_mmap_read(prev_head, 0, NULL);

	ioctl(perf_fd, PERF_EVENT_IOC_REFRESH, 1);
}

void setupSampling(void) {
  struct perf_event_attr pe;
	memset(&pe, 0, sizeof(struct perf_event_attr));

  pe.type = PERF_TYPE_RAW;
  pe.size = sizeof(struct perf_event_attr);

	// config: this field is set depending on the type of bridge in the processor 
	// and whether we would like to sample load/store accesses.
	// For more information see the Intel 64 and IA-32 Architectures Software Developer's Manual:
	// http://courses.cs.washington.edu/courses/cse451/15au/readings/ia32-3.pdf
  //pe.config = 0x1cd;		// samples load accesses
  pe.config = 0x2cd;			// samples store accesses

	//Sample_period/freq: Setting the rate of recording. 
	//For perf, it generates an overflow and start writing to mmap buffer in a fixed frequency set here.
  pe.sample_period = 1000;
  pe.sample_freq = 1000;

	//Sample_type: Specifies which should be sampled in an access. 
	//For c10f, we sampled Pid, Tid, Addr, Time, Weight, Data_src.
  //pe.sample_type = 0xc10f;
  //pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR |
	//									PERF_SAMPLE_PERIOD | PERF_SAMPLE_WEIGHT | PERF_SAMPLE_DATA_SRC;
	pe.sample_type = sample_type;

	//This field specifies the format of the data returned by
	//read() on a perf_event_open() file descriptor. 
  pe.read_format = read_format;

	//Disabled: whether the counter starts with disabled/enabled status. 
  pe.disabled = 1;

	//Pinned: The pinned bit specifies that the counter should always be on
	//the CPU if at all possible.
  pe.pinned = 0;

	//Inherit:The inherit bit specifies that this counter should count
	//events of child tasks as well as the task specified.
	//WARNING: It is this property that breaks perf_event_open(&pe, 0, -1...)
  pe.inherit = 0;

	//Exclucive:The exclusive bit specifies that when this counter's group is
	//on the CPU, it should be the only group using the CPU's counters.
  pe.exclusive = 0;

	//Exclude_xxx: Do not sample a specified side of events, 
	//user, kernel, or hypevisor
  pe.exclude_user = 0;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;

	//Exclude_idle: If set, don't count when the CPU is idle.
  pe.exclude_idle = 1;

	//Mmap: The mmap bit enables generation of PERF_RECORD_MMAP samples
	//for every mmap() call that has PROT_EXEC set.
  pe.mmap = 1;

	//Comm: The comm bit enables tracking of process command name as
	//modified by the exec() and prctl(PR_SET_NAME) system calls as
	//well as writing to /proc/self/comm.
	//If comm_exec is also set, then the misc flag
	//PERF_RECORD_MISC_COMM_EXEC can be used to
	//differentiate the exec() case from the others.
	// FIXME
  pe.comm = 1;

	//Set this field to use frequency instead of period, see above.
  pe.freq = 1;

	//Inherit_stat: This bit enables saving of event counts on context switch for
	//inherited tasks. So it is only useful when 'inherit' set to 1.
 	pe.inherit_stat = 1;

	//When 'disable' and this bit is set, a counter is automatically enabled after a call to exec().
  pe.enable_on_exec = 1;

	//Task: If this bit is set, then fork/exit notifications are included in the ring buffer.
  pe.task = 0;

	//Watermark: If set, have an overflow notification happen when we cross the
	//wakeup_watermark boundary.
  pe.watermark = 0;

	//Precise_ip: This controls the amount of skid. See perf_event.h
  pe.precise_ip = 2;

	//Mmap_data: This enables generation of PERF_RECORD_MMAP samples for mmap() calls 
	//that do not have PROT_EXEC set
  //pe.mmap_data = 1;
  //TODO: turned off by me during testing -- sam
  pe.mmap_data = 0;

	//Sample_id_all: TID, TIME, ID, STREAM_ID, and CPU added to every sample.
  pe.sample_id_all = 0;

	//Exclude_xxx: Sample guest/host instances or not.
  pe.exclude_host = 0;
  pe.exclude_guest = 0;

	//Exclude_callchain_xxx: exclude callchains from kernel/user.
  pe.exclude_callchain_kernel = 1; // exclude kernel callchains
  pe.exclude_callchain_user = 1; // exclude user callchains

	//Mmap2:Generate an extended executable mmap record that contains
	//enough additional information to uniquely identify shared
	//mappings. Set 'mmap' before setting this.
  //TODO: turned off by me during testing -- sam
  pe.mmap2 = 0;

 	pe.comm_exec = 1; // flag comm events that are due to an exec

	//wakeup_xxx: This union sets how many samples (wakeup_events) or bytes
	//(wakeup_watermark) happen before an overflow notification happens.
 	pe.wakeup_events = 0;
  pe.wakeup_watermark = 0;

	//bp_type: This chooses the breakpoint type.
  pe.bp_type = HW_BREAKPOINT_EMPTY;

	//bp_addr: For write/read samples, it collects memory address.
  //pe.bp_addr = 0x3;

	//Expanding config. See 'config'.
  //pe.config1 = 0x3;
  //pe.config2 = 0;

	//bp_len: length of the breakpoint being measured if type
	//is PERF_TYPE_BREAKPOINT.
  pe.bp_len = 0;

	//branch_sample_type: If PERF_SAMPLE_BRANCH_STACK is enabled, then this specifies
	//what branches to include in the branch record.
  pe.branch_sample_type = 0;

	//sample_regs_user:  This bit mask defines the set of user 
	//CPU registers to dump on samples.
  pe.sample_regs_user = 0;

	//sample_stack_user: This defines the size of the user stack to dump if
	//PERF_SAMPLE_STACK_USER is specified.
  pe.sample_stack_user = 0;

  // Create the perf_event for this thread on all CPUs with no event group
	// Second parameter (target thread): 0=self, -1=cpu-wide mode
	// Third parameter: cpu: -1 == any
	//perf_fd = perf_event_open(&pe, getpid(), k, -1, 0);

	perf_fd = perf_event_open(&pe, 0, -1, -1, 0);
	if(perf_fd == -1) {
		fprintf(stderr, "Failed to open perf event file: %s\n", strerror(errno));
		abort();
	}

	// Setting up 9 pages to pass information about a trap
	if((our_mmap = mmap(NULL, MAPSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, perf_fd, 0)) == MAP_FAILED) {
		fprintf(stderr, "ERROR: mmap failed on perf_fd=%d: %s", perf_fd, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Set the perf_event file to async mode
	if(fcntl(perf_fd, F_SETFL, O_RDWR|O_NONBLOCK|O_ASYNC) == -1) {
		fprintf(stderr, "Failed to set perf event file to ASYNC mode: %s\n", strerror(errno));
		abort();
	}
 
	// Deliver the signal to this thread
	struct f_owner_ex owner = {F_OWNER_TID, tid};
	/*
	owner.type = F_OWNER_TID;
	owner.pid = tid;
	*/
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
