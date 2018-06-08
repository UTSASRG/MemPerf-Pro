/**
 * @file libmemperf.cpp
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 * @author Sam Silvestro <sam.silvestro@utsa.edu>
 */

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#define _GNU_SOURCE         /* See feature_test_macros(7) */

#include <dlfcn.h>
#include <malloc.h>
#include <alloca.h>

#include <sys/mman.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <unistd.h>
#include <execinfo.h>

#include <map>
#include <cstdlib>
#include <vector>

#include "memsample.h"
#include "real.hh"
#include "xthread.hh"
#include "hashmap.hh"
#include "hashfuncs.hh"
#include "spinlock.hh"
#include "selfmap.hh"

#define CALLSITE_MAXIMUM_LENGTH 10

typedef HashMap<uint64_t, ObjectTuple*, spinlock> HashMapX;
spinlock mmap_lock;
int numMmaps;
size_t mmapSize;

__thread thread_data thrData;

HashMapX mapCallsiteStats;

extern char data_start;
extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
extern char __executable_start;

bool const debug = false;
bool mallocInitialized = false;
uint64_t min_pos_callsite_id;

// Rich
int numNewAlloc = 0;
int numReuseAlloc = 0;
int numFrees = 0;
int numMallocs = 0;
unsigned long cyclesNewAlloc = 0;
unsigned long cyclesReuseAlloc = 0;
unsigned long cyclesFree = 0;
unsigned long mmapThreshold;
bool bumpPointer = false;
bool bibop = false;
static volatile bool inMalloc = false;
static volatile bool inRealloc = false;
static volatile bool inRealMain = false;
FILE* objectStats;
FILE* classInfo;
FILE* pageInfo;
FILE* testInfo;
spinlock mallocLock;
spinlock reallocLock;

void getAllocStyle ();
void getClassSizes ();
void test1 ();
ObjectTuple* newObjectTuple (int numAllocs, size_t objectSize);

// Variables used by our pre-init private allocator
char tmpbuf[TEMP_BUF_SIZE];
unsigned int numTempAllocs = 0;
unsigned int tmppos = 0;

struct stack_frame {
	struct stack_frame * prev;	// pointing to previous stack_frame
	void * caller_address;		// the address of caller
};

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main;

extern "C" {
	// Function prototypes
	addrinfo addr2line(void * addr);
	size_t getTotalAllocSize(size_t sz);
	void exitHandler();
	inline void getCallsites(void **callsites);

	// Function aliases
	void free(void *) __attribute__ ((weak, alias("yyfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("yycalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("yymalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("yyrealloc")));
	void * valloc(size_t) __attribute__ ((weak, alias("yyvalloc")));
	void * mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) __attribute__ ((weak, alias("yymmap")));
	void * aligned_alloc(size_t, size_t) __attribute__ ((weak,
				alias("yyaligned_alloc")));
	void * memalign(size_t, size_t) __attribute__ ((weak, alias("yymemalign")));
	void * pvalloc(size_t) __attribute__ ((weak, alias("yypvalloc")));
	void * alloca(size_t) __attribute__ ((weak, alias("yyalloca")));
	int posix_memalign(void **, size_t, size_t) __attribute__ ((weak,
				alias("yyposix_memalign")));
}

__attribute__((constructor)) void initializer() {
	if(mallocInitialized) { return; }

	// Ensure we are operating on a system using 64-bit pointers.
	// This is necessary, as later we'll be taking the low 8-byte word
	// of callsites. This could obviously be expanded to support 32-bit systems
	// as well, in the future.
	size_t ptrSize = sizeof(void *);
	if(ptrSize != EIGHT_BYTES) {
		fprintf(stderr, "error: unsupported pointer size: %zu\n", ptrSize);
		abort();
	}

	mmap_lock.init();
	mallocLock.init ();
	reallocLock.init ();

	// Calculate the minimum possible callsite ID by taking the low four bytes
	// of the start of the program text and repeating them twice, back-to-back,
	// resulting in eight bytes which resemble: 0x<lowWord><lowWord>
	uint64_t btext = (uint64_t)&__executable_start & 0xFFFFFFFF;
	min_pos_callsite_id = (btext << 32) | btext;

	/*
	// Allocate memory for our pre-init temporary allocator
	if((tmpbuf = (char *)mmap(NULL, TEMP_BUF_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        perror("error: unable to allocate temporary memory");
		abort();
  }
	*/

	Real::initializer();
	//selfmap::getInstance().getTextRegions();
	mallocInitialized = true;
	void * program_break = sbrk(0);
	thrData.stackStart = (char *)__builtin_frame_address(0);
	thrData.stackEnd = (char *)__libc_stack_end;

	// Initialize hashmap
	mapCallsiteStats.initialize(HashFuncs::hashCallsiteId,
			HashFuncs::compareCallsiteId, 4096);

	// Generate the name of our output file, then open it for writing.
	char outputFile[MAX_FILENAME_LEN];
	pid_t pid = getpid();
	snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmemperf_pid_%d_tid_%d.txt",
		program_invocation_name, pid, pid);
	// Will overwrite current file; change the fopen flag to "a" for append.
	thrData.output = fopen(outputFile, "w");
	if(thrData.output == NULL) {
		perror("error: unable to open output file to write");
		return;
	}

	fprintf(thrData.output, ">>> stack start @ %p, stack end @ %p\n",
			thrData.stackStart, thrData.stackEnd);
	fprintf(thrData.output, ">>> program break @ %p\n", program_break);
	fflush(thrData.output);

	// Determines allocator style: bump pointer or bibop
	getAllocStyle ();
	
	// Determine class size of 
	//if (bibop) getClassSizes ();
	if (bibop) test1 ();
}

__attribute__((destructor)) void finalizer() {
	fclose(thrData.output);
}

void writeHashMap (void) {

	HashMapX::iterator iterator;
		for (iterator = mapCallsiteStats.begin();
			  iterator != mapCallsiteStats.end();
			  iterator++) {
			
			//ObjectTuple* tuple = iterator.getData();	

		}
}

void writeAllocData () {

	fprintf (thrData.output, ">>> Number of mmap calls: %d\n", numMmaps);
	fprintf (thrData.output, ">>> Total size of mmap calls: %zu\n", mmapSize);
	fprintf (thrData.output, ">>> Detected mmap threshold: %zu\n", mmapThreshold);
	fprintf (thrData.output, ">>> Number of malloc calls: %d\n", numMallocs);
	fprintf (thrData.output, ">>> Number of new allocations: %d\n", numNewAlloc);
	fprintf (thrData.output, ">>> Number of re allocations:  %d\n", numReuseAlloc);
	fprintf (thrData.output, ">>> Number of frees:           %d\n", numFrees);

	if (numNewAlloc > 0)
		fprintf (thrData.output, ">>> Avg cycles of new alloc:      %lu clock cycles\n",
					(cyclesNewAlloc / (long)numNewAlloc));

	else
		fprintf (thrData.output, ">>> Avg cycles of new alloc:      N/A\n");

	if (numReuseAlloc > 0) 
		fprintf (thrData.output, ">>> Avg cycles of reuse alloc:    %lu clock cycles\n",
					(cyclesReuseAlloc / (long)numReuseAlloc));

	else 
		fprintf (thrData.output, ">>> Avg cycles of reuse alloc:    N/A\n");

	if (numFrees > 0)
		fprintf (thrData.output, ">>> Avg cycles for free:          %lu clock cycles \n",
					(cyclesFree / (long)numFrees));

	else
		fprintf (thrData.output, ">>> Avg cycles for free:          N/A\n");

	fflush (thrData.output);
}

void exitHandler() {

	inRealMain = false;

	doPerfRead();
	fflush(thrData.output);
	writeAllocData ();
}

// MemPerf's main function
int libmemperf_main(int argc, char ** argv, char ** envp) {
	// Register our cleanup routine as an on-exit handler.
	atexit(exitHandler);

	initSampling();
	
	inRealMain = true;
	return real_main(argc, argv, envp);
}

extern "C" int __libc_start_main(main_fn_t, int, char **, void (*)(),
		void (*)(), void (*)(), void *) __attribute__((weak,
			alias("libmemperf_libc_start_main")));

extern "C" int libmemperf_libc_start_main(main_fn_t main_fn, int argc,
		char ** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(),
		void * stack_end) {
	auto real_libc_start_main =
		(decltype(__libc_start_main) *)dlsym(RTLD_NEXT, "__libc_start_main");
	real_main = main_fn;
	return real_libc_start_main(libmemperf_main, argc, argv, init, fini,
			rtld_fini, stack_end);
}

// Memory management functions
extern "C" {
	void * yymalloc(size_t sz) {
		// Small allocation routine designed to service malloc requests made by
		// the dlsym() function, as well as code running prior to dlsym(). Due
		// to our linkage alias which redirects malloc calls to yymalloc
		// located in this file; when dlsym calls malloc, yymalloc is called
		// instead. Without this routine, yymalloc would simply rely on
		// Real::malloc to fulfill the request, however, Real::malloc would not
		// yet be assigned until the dlsym call returns. This results in a
		// segmentation fault. To remedy the problem, we detect whether the Real
		// has finished initializing; if it has not, we fulfill malloc requests
		// using a memory mapped region. Once dlsym finishes, all future malloc
		// requests will be fulfilled by Real::malloc, which itself is a
		// reference to the real glibc malloc routine.
		if(!mallocInitialized) {
			if((tmppos + sz) < TEMP_BUF_SIZE) {
				void * retptr = (void *)(tmpbuf + tmppos);
				tmppos += sz;
				numTempAllocs++;
				return retptr;
			} else {
				fprintf(stderr, "error: global allocator out of memory\n");
				fprintf(stderr, "\t requested size = %zu, total size = %d, "
					"total allocs = %u\n", sz, TEMP_BUF_SIZE, numTempAllocs);
				abort();
			}
		}

		void* objAlloc;

		if (inRealMain) {

			inMalloc = true;
			uint64_t before = rdtscp ();
			objAlloc = Real::malloc(sz);
			uint64_t after = rdtscp ();
			uint64_t cyclesForMalloc = after - before;
			uint64_t address = reinterpret_cast <uint64_t> (objAlloc);

			ObjectTuple* found_value;

			mallocLock.lock ();

			if(mapCallsiteStats.find(address, &found_value)) {
				found_value->numAllocs++;
				found_value->size = sz;
				numReuseAlloc ++;
				cyclesReuseAlloc += cyclesForMalloc;
			}
			else {
				ObjectTuple* tuple = newObjectTuple (1, sz);
				mapCallsiteStats.insertIfAbsent(address, tuple);
				numNewAlloc ++;
				cyclesNewAlloc += cyclesForMalloc;
			}

			numMallocs++;
			inMalloc = false;
			mallocLock.unlock ();
		}
		
		else objAlloc = Real::malloc (sz);

		return objAlloc;
	}

	void * yycalloc(size_t nelem, size_t elsize) {
		
		void * ptr = NULL;
		ptr = malloc (nelem * elsize);
		if(ptr)
			memset(ptr, 0, nelem * elsize);
		return ptr;
	}

	void yyfree(void * ptr) {
		if(ptr == NULL)
			return;

		// Determine whether the specified object came from our global buffer;
		// only call Real::free() if the object did not come from here.
		if((ptr >= (void *)tmpbuf) &&
				(ptr <= (void *)(tmpbuf + TEMP_BUF_SIZE))) {
			numTempAllocs--;
			if(numTempAllocs == 0) {
					tmppos = 0;
			}
		} else {
			
			if (inRealMain) {
				uint64_t before = rdtscp ();
				Real::free(ptr);
				uint64_t after = rdtscp ();
				numFrees ++;
				cyclesFree += (after - before);
			}
			else {
				Real::free (ptr);
			}
		}
	}

	inline void logUnsupportedOp() {
		fprintf(thrData.output,
				"ERROR: call to unsupported memory function: %s\n",
				__FUNCTION__);
	}
	void * yyalloca(size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * yyvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	int yyposix_memalign(void **memptr, size_t alignment, size_t size) {
		logUnsupportedOp();
		return -1;
	}
	void * yyaligned_alloc(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * yymemalign(size_t alignment, size_t size) {
		logUnsupportedOp();
		return NULL;
	}
	void * yypvalloc(size_t size) {
		logUnsupportedOp();
		return NULL;
	}

	void * yyrealloc(void * ptr, size_t sz) {
		if(!mallocInitialized) {
			if(ptr == NULL)
				return yymalloc(sz);
			yyfree(ptr);
			return yymalloc(sz);
		}

		reallocLock.lock ();
		inRealloc = true;
		void * reptr = Real::realloc(ptr, sz);
		inRealloc = false;
		reallocLock.unlock ();

		return reptr;
	}

	inline void getCallsites(void **callsites) {
		int i = 0;
		void * btext = &__executable_start;
		void * etext = &data_start;

		// Fetch the frame address of the topmost stack frame
		struct stack_frame * current_frame =
			(struct stack_frame *)(__builtin_frame_address(0));

		// Initialize the prev_frame pointer to equal the current_frame. This
		// simply ensures that the while loop below will be entered and
		// executed and least once
		struct stack_frame * prev_frame = current_frame;

		// Initialize the array elements
		callsites[0] = (void *)NULL;
		callsites[1] = (void *)NULL;

		// Loop condition tests the validity of the frame address given for the
		// previous frame by ensuring it actually points to a location located
		// on the stack
		while((i < 2) && ((void *)prev_frame <= (void *)thrData.stackStart) &&
				(prev_frame >= current_frame)) {
			// Inspect the return address belonging to the previous stack frame;
			// if it's located in the program text, record it as the next
			// callsite
			void * caller_addr = prev_frame->caller_address;
			if((caller_addr >= btext) && (caller_addr <= etext)) {
				callsites[i++] = caller_addr;
			}
			// Walk the prev_frame pointer backward in preparation for the
			// next iteration of the loop
			prev_frame = prev_frame->prev;
		}
	}

	/*
	 * This function returns the total usable size of an object allocated
	 * using glibc malloc (and therefore it does not include the space occupied
	 * by its 8 byte header).
 	 */
	size_t getTotalAllocSize(size_t sz) {
		size_t totalSize, usableSize;

		// Smallest possible total object size (including header) is 32 bytes,
		// thus making the total usable object size equal to 32-8=24 bytes.
		if(sz <= 24) {
			return 24;
		}

		// Calculate a total object size that is double-word aligned.
		totalSize = sz + 8;
		if(totalSize % 16 != 0) {
			totalSize = 16 * (((sz + 8) / 16) + 1);
			usableSize = totalSize - 8;
		} else {
			usableSize = sz;
		}

		return usableSize;
	}

	addrinfo addr2line(void * addr) {
		static bool initialized = false;
		static int fd[2][2];
		char strCallsite[20];
		char strInfo[512];
		addrinfo info;

		if(!initialized) {
			if((pipe(fd[0]) == -1) || (pipe(fd[1]) == -1)) {
				perror("error: unable to create pipe for addr2line\n");
				fprintf(thrData.output,
						"error: unable to create pipe for addr2line\n");
				strcpy(info.exename, "error");
				info.lineNum = 0;
				return info;
			}

			pid_t parent;
			switch(parent = fork()) {
				case -1:
					perror("error: unable to fork addr2line process\n");
					fprintf(thrData.output,
							"error: unable to fork addr2line process\n");
					strcpy(info.exename, "error");
					info.lineNum = 0;
					return info;
				case 0:		// child
					dup2(fd[1][0], STDIN_FILENO);
					dup2(fd[0][1], STDOUT_FILENO);
					// Close unneeded pipe ends for the child
					close(fd[0][0]);
					close(fd[1][1]);
					execlp("addr2line", "addr2line", "-s", "-e",
							program_invocation_name, "--", (char *)NULL);
					exit(EXIT_FAILURE);	// if we're still here then exec failed
					break;
				default:	// parent
					// Close unneeded pipe ends for the parent
					close(fd[0][1]);
					close(fd[1][0]);
					initialized = true;
			}
		}

		sprintf(strCallsite, "%p\n", addr);
		int szToWrite = strlen(strCallsite);
		if(write(fd[1][1], strCallsite, szToWrite) < szToWrite) {
			perror("error: incomplete write to pipe facing addr2line\n");
			fprintf(thrData.output,
					"error: incomplete write to pipe facing addr2line\n");
		}

		if(read(fd[0][0], strInfo, 512) == -1) {
			perror("error: unable to read from pipe facing addr2line\n");
			fprintf(thrData.output,
					"error: unable to read from pipe facing addr2line\n");
			strcpy(info.exename, "error");
			info.lineNum = 0;
			return info;
		}

		// Tokenize the return string, breaking apart by ':'.
		// Take the second token, which will be the line number.
		// Only copies the first 14 characters of the file name in order to
		// prevent misalignment in the program output.
		char * token = strtok(strInfo, ":");
		strncpy(info.exename, token, 14);
		info.exename[14] = '\0';	// null terminate the exename field
		token = strtok(NULL, ":");
		info.lineNum = atoi(token);

		return info;
	}

	// Intercept thread creation
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr,
			void *(*start_routine)(void *), void * arg) {
		return xthread::thread_create(tid, attr, start_routine, arg);
	}
}

// Richard
// Create tuple for hashmap
ObjectTuple* newObjectTuple (int numAllocs, size_t objectSize) {

	ObjectTuple* objectTuple = (ObjectTuple*) Real::malloc (sizeof (ObjectTuple));	
	objectTuple->numAllocs = numAllocs;
	objectTuple->size = objectSize;
	return objectTuple;
}

// Try to figure out which allocator is being used
void getAllocStyle () {

	void* add1 = Real::malloc (8);
	void* add2 = Real::malloc (2048);

	fprintf (thrData.output, ">>> Object1 (8B): %p\n>>> Object2 (2KB): %p\n", add1, add2);

	long address1 = reinterpret_cast<long> (add1);
	long address2 = reinterpret_cast<long> (add2);

	long address1Page = address1 / 4096;
	long address2Page = address2 / 4096;

	fprintf (thrData.output, ">>> Object1 page: %ld, Object2 page: %ld\n", address1Page, address2Page);

	if ((address1Page - address2Page) != 0) {

		bibop = true;
		
		fprintf (thrData.output, ">>> Objects are not on same page\n");
		fprintf (thrData.output, ">>> Allocator Style: BIBOP\n");
	}

	else {

		bumpPointer = true;
		fprintf (thrData.output, ">>> Objects are on same page.\n");
		fprintf (thrData.output, ">>> Allocator Style: bump-pointer\n");
	}
}

void getClassSizes () {

	objectStats = fopen ("test/files/objectStats.txt", "w");
	pageInfo = fopen ("test/files/pageInfo.txt", "w");
	classInfo = fopen ("test/files/classInfo.txt", "w");

	std::vector <uint64_t> classSizes;
	int size = 8;
	
	void* malloc1;

	long currentAddress = 0;
	long currentPage = 0;

	std::map <long, int> pages;
	
	for (int i = 1; i <= 1000; i++) {

		malloc1 = Real::malloc (size);

		currentAddress = reinterpret_cast <long> (malloc1);
		currentPage = currentAddress / 4096;
		
		auto found = pages.find (currentPage);		

		if (found == pages.end ()) 
			pages.emplace (currentPage, 1);

		else
			pages[currentPage] = (found->second) + 1;

		fprintf (objectStats, "obj %4d >>> address: %lx  page: %lu\n",
					i, currentAddress, currentPage);
	}

	for (auto page = pages.begin(); page != pages.end(); ++page) {

		fprintf (pageInfo, "page %lu has %d objects\n", page->first, page->second);
	}

	fclose (objectStats);
	fclose (pageInfo);
	fclose (classInfo);	
}

void test1 () {

	void* oldAddress;
	void* newAddress;
	long oldAddr = 0, newAddr = 0;
	size_t oldSize = 8, newSize = 8;
	char fileName [32];
	double exactPage = 0;
	double kiloB = 0;
	std::vector <uint64_t> classSizes;

	pid_t pid = getpid ();

	snprintf (fileName, 32, "test/files/testInfo_%d.txt", pid);

	testInfo = fopen (fileName, "w");

	oldAddress = Real::malloc (oldSize);
	oldAddr = reinterpret_cast <long> (oldAddress);

	for (int i = 0; i < 20000; i++) {

		newSize += 8;
		newAddress = Real::realloc (oldAddress, newSize);
		newAddr = reinterpret_cast <long> (newAddress);

		exactPage = newAddr / 4096.0;
		
		if ((newAddr != oldAddr)) {

			if (classSizes.empty ()) 
				classSizes.push_back (oldSize);

			else {

				classSizes.push_back (oldSize);
				kiloB = oldSize / 1024.0;
				fprintf (testInfo, "oldSize: %lu - %.2f KB | old=%p, new=%p, realloc "
										 "%zu B returned new address. Page %.2f\n",
							oldSize, kiloB, oldAddress, newAddress, newSize, exactPage);
			}
		}

		oldAddr = newAddr;
		oldAddress = newAddress;
		oldSize = newSize;
	}

	classInfo = fopen ("test/files/classInfo.txt", "a");
	fprintf (classInfo, "Possible Class Sizes: ");

	if (!classSizes.empty ()) {

		for (auto cSize = classSizes.begin (); cSize != classSizes.end (); cSize++) 
			fprintf (classInfo, "%zu ", *cSize);
	}

	fprintf (classInfo, "\n\n");
	fclose (classInfo);
	fclose (testInfo);
}

inline bool isAllocatorInCallStack() {
		void * array[256];
		int frames = backtrace(array, 256);
		int allocatorLevel = -2;

		char buf[256];

		if(frames >= 256) {
				fprintf(stderr, "WARNING: callstack may have been truncated\n");
		} else if(frames == 0) {
				fprintf(stderr, "WARNING: callstack depth was detected as zero\n");
		}

		printf("backtrace, frames = %d:\n", frames);
		for(int i = 0; i < frames; i++) {
				void * addr = array[i];
				printf("   level %3d: addr = %p\n", i, addr);

				if(selfmap::getInstance().isAllocator(addr)) {
						allocatorLevel = i;
				}
		}

		return((allocatorLevel != -2) && (allocatorLevel != frames - 1));
}
/*
inline bool isAllocatorInCallStack() {
        // Fetch the frame address of the topmost stack frame
        struct stack_frame * current_frame =
            (struct stack_frame *)(__builtin_frame_address(0));

        // Initialize the prev_frame pointer to equal the current_frame. This
        // simply ensures that the while loop below will be entered and
        // executed and least once
        struct stack_frame * prev_frame = current_frame;

        void * stackEnd = thrData.stackEnd;

				int allocatorLevel = -1;

				void * lastSeenAddress = NULL;

        int cur_depth = 0;
        while(((void *)prev_frame <= stackEnd) &&
                (prev_frame >= current_frame) && (cur_depth < CALLSITE_MAXIMUM_LENGTH)) {
						void * caller_address = prev_frame->caller_address;
						lastSeenAddress = caller_address;

						if(selfmap::getInstance().isAllocator(caller_address)) {
								printf("current caller is allocator: caller = %p, frame = %p\n", caller_address, prev_frame);
								//return true;
								//hasSeenAllocator = true;
								allocatorLevel = cur_depth;
						} else {
								printf("current caller is NOT the allocator: caller = %p, frame = %p\n", caller_address, prev_frame);
						}

            //in some case, "prev" address is the same as current address
            //or there is recursion 
            if(prev_frame == prev_frame->prev) {
								cur_depth++;
                break;
            }

            // Walk the prev_frame pointer backward in preparation for the
            // next iteration of the loop
            prev_frame = prev_frame->prev;

            cur_depth++;
				}

				printf("allocatorLevel = %d, cur_depth = %d\n", allocatorLevel, cur_depth);
				if((allocatorLevel > -1) && (allocatorLevel < cur_depth - 1)) {
				//if(hasSeenAllocator && !selfmap::getInstance().isAllocator(lastSeenAddress)) {
						return true;
				}	

				return false;
}
*/

extern "C" void * yymmap(void *addr, size_t length, int prot, int flags,
				int fd, off_t offset) {
		initializer();
	
		mmap_lock.lock();
		void * retval = Real::mmap(addr, length, prot, flags, fd, offset);

		// if call came from the allocator
		if (inMalloc || inRealloc) {

			numMmaps++;
			mmapSize += length;
			if (numMmaps == 1)
				mmapThreshold = length;
		}

		mmap_lock.unlock();
		return retval;

		
/*
		if(isAllocatorInCallStack()) {
				printf("*** yymmap call came from the allocator\n");
				mmap_lock.lock();
				numMmaps++;
				mmapSize += length;
				void * retval = Real::mmap(addr, length, prot, flags, fd, offset);
				mmap_lock.unlock();
				return retval;
		} else {
				return Real::mmap(addr, length, prot, flags, fd, offset);
		}

*/
}
