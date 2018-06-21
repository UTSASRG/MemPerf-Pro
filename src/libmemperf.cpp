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

spinlock mmap_lock;
int num_mmaps;

__thread thread_data thrData;

extern char data_start;
extern char _etext;
extern char * program_invocation_name;
extern void * __libc_stack_end;
extern char __executable_start;

const char *CLASS_SIZE_FILE_NAME = "class_sizes.txt";
FILE *classSizeFile;

bool const debug = false;

enum initStatus{
    INIT_ERROR = -1,
    NOT_INITIALIZED = 0, 
    IN_PROGRESS = 1, 
    INITIALIZED = 2
};
initStatus mallocInitialized = NOT_INITIALIZED;

uint64_t min_pos_callsite_id;

#define MAX_CLASS_SIZE 65536
#define TEMP_MEM_SIZE 2048000  // 8MB
#define THREAD_BUF_SIZE 2048000	// 8 MB

bool bumpPointer = false;
bool bibop = false;
bool inGetMmapThreshold = false;
bool usingMalloc = false;
bool usingRealloc = false;
bool inRealloc = false;
bool mapsInitialized = false;

int numFrees = 0;
int numMallocs = 0;
int new_address = 0;
int reused_address = 0;
int num_pthread_mutex_locks = 0;
int trylockAttempts = 0;

spinlock mallocLock;
spinlock reallocLock;
spinlock myMallocLock;
spinlock activeThreadLock;
spinlock freeLock;

void* myMalloc (size_t size);
void myFree (void* ptr);

typedef struct LockContention {
	int contention;
	int maxContention;
} LC;

LC* newLC () {
	LC* lc = (LC*) myMalloc (sizeof(LC));
	lc->contention = 1;
	lc->maxContention = 1;
	return lc;
}

HashMap <pid_t, bool, spinlock> activeThreads;
HashMap <uint64_t, bool, spinlock> addressUsage;
HashMap <uint64_t, LC*, spinlock> lockUsage;

unsigned long cyclesFree = 0;
unsigned long cyclesNewAlloc = 0;
unsigned long cyclesReuseAlloc = 0;
unsigned long malloc_mmap_threshold = 0;
unsigned long realloc_mmap_threshold = 0;

void getAllocStyle ();
void getClassSizes ();
void getMmapThreshold ();
void writeAllocData ();
void writeContention ();
ObjectTuple* newObjectTuple (int numAllocs, size_t objectSize);

// Variables used by our pre-init private allocator
char tmpbuf[TEMP_MEM_SIZE];
unsigned int numTempAllocs = 0;
unsigned int tmppos = 0;

// Variables for activeThreads hashmap only
char threadBuf [THREAD_BUF_SIZE];
unsigned int numActiveThreads = 0;
unsigned int threadBufPos = 0;

void* allocNewThread (size_t size) {

	activeThreadLock.lock ();
	void* retptr;
	if((threadBufPos + size) < THREAD_BUF_SIZE) {
		retptr = (void *)(threadBuf + threadBufPos);
		threadBufPos += size;
		numActiveThreads++;
	} else {
		fprintf(stderr, "error: activeThreads allocator out of memory\n");
		fprintf(stderr, "\t requested size = %zu, total size = %d, "
							 "total allocs = %u\n", size, THREAD_BUF_SIZE*4, numTempAllocs);
		abort();
	}
	activeThreadLock.unlock ();
	return retptr;
}

void freeThread (void* ptr) {

	if (ptr == NULL) return;	

	activeThreadLock.lock();
	if ((ptr >= threadBuf) &&
		(ptr <= (threadBuf + THREAD_BUF_SIZE))) {
		numActiveThreads--;
		if(numActiveThreads == 0) threadBufPos = 0;
	}
	activeThreadLock.unlock();
}

struct stack_frame {
	struct stack_frame * prev;	// pointing to previous stack_frame
	void * caller_address;		// the address of caller
};

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main_memperf;

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

__attribute__((constructor)) initStatus initializer() {
    
	if(mallocInitialized == INITIALIZED || mallocInitialized == IN_PROGRESS){
                return mallocInitialized;
        }
        mallocInitialized = IN_PROGRESS;

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
	myMallocLock.init ();
	activeThreadLock.init ();
	freeLock.init ();

	// Calculate the minimum possible callsite ID by taking the low four bytes
	// of the start of the program text and repeating them twice, back-to-back,
	// resulting in eight bytes which resemble: 0x<lowWord><lowWord>
	uint64_t btext = (uint64_t)&__executable_start & 0xFFFFFFFF;
	min_pos_callsite_id = (btext << 32) | btext;


	//tmpbuf is just declared as a char[TEMP_MEM_SIZE] right now

/*
	// Allocate memory for our pre-init temporary allocator
	if((tmpbuf = mmap (NULL, TEMP_MEM_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        perror("error: unable to allocate temporary memory");
		abort();
	}
*/

	RealX::initializer();

	selfmap::getInstance().getTextRegions();

	mallocInitialized = INITIALIZED;
	void * program_break = sbrk(0);
	thrData.stackStart = (char *)__builtin_frame_address(0);
	thrData.stackEnd = (char *)__libc_stack_end;

	// Generate the name of our output file, then open it for writing.
	char outputFile[MAX_FILENAME_LEN];
	pid_t pid = getpid();
//	snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmemperf_pid_%d_tid_%d.txt",
//		program_invocation_name, pid, pid);
	snprintf(outputFile, MAX_FILENAME_LEN, "%s_libmemperf_pid_%d_main_thread.txt",
		program_invocation_name, pid);
	// Will overwrite current file; change the fopen flag to "a" for append.
	thrData.output = fopen(outputFile, "w");
	if(thrData.output == NULL) {
		perror("error: unable to open output file to write");
		return INIT_ERROR;
	}

	fprintf(thrData.output, ">>> stack start @ %p, stack end @ %p\n",
			thrData.stackStart, thrData.stackEnd);
	fprintf(thrData.output, ">>> program break @ %p\n", program_break);
	fflush(thrData.output);

	addressUsage.initialize(HashFuncs::hashCallsiteId,
			HashFuncs::compareCallsiteId, 4096);

	activeThreads.initialize(HashFuncs::hashInt,
			HashFuncs::compareInt, 4096);

	lockUsage.initialize(HashFuncs::hashCallsiteId,
						 HashFuncs::compareCallsiteId, 4096);

	mapsInitialized = true;

	// Determines allocator style: bump pointer or bibop
	getAllocStyle ();
	
	// Determine class size for bibop allocator
	if (bibop) getClassSizes ();

	//get mmap threshold
	getMmapThreshold ();
        
   return mallocInitialized;
}

__attribute__((destructor)) void finalizer_memperf() {
	fclose(thrData.output);
}

void exitHandler() {

	doPerfRead();
	fflush(thrData.output);
	writeAllocData ();
	writeContention ();
}

// MemPerf's main function
int libmemperf_main(int argc, char ** argv, char ** envp) {
	// Register our cleanup routine as an on-exit handler.
	atexit(exitHandler);

	initSampling();
	
	return real_main_memperf(argc, argv, envp);
}

extern "C" int __libc_start_main(main_fn_t, int, char **, void (*)(),
		void (*)(), void (*)(), void *) __attribute__((weak,
			alias("libmemperf_libc_start_main")));

extern "C" int libmemperf_libc_start_main(main_fn_t main_fn, int argc,
		char ** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(),
		void * stack_end) {
	auto real_libc_start_main =
		(decltype(__libc_start_main) *)dlsym(RTLD_NEXT, "__libc_start_main");
	real_main_memperf = main_fn;
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
		// RealX::malloc to fulfill the request, however, RealX::malloc would not
		// yet be assigned until the dlsym call returns. This results in a
		// segmentation fault. To remedy the problem, we detect whether the RealX
		// has finished initializing; if it has not, we fulfill malloc requests
		// using a memory mapped region. Once dlsym finishes, all future malloc
		// requests will be fulfilled by RealX::malloc, which itself is a
		// reference to the real glibc malloc routine.
		if(mallocInitialized != INITIALIZED) {
			if((tmppos + sz) < TEMP_MEM_SIZE) {
				void * retptr = (void *)(tmpbuf + tmppos);
				tmppos += sz;
				numTempAllocs++;
				return retptr;
			} else {
				fprintf(stderr, "error: temp allocator out of memory\n");
				fprintf(stderr, "\t requested size = %zu, total size = %d, "
					"total allocs = %u\n", sz, TEMP_MEM_SIZE, numTempAllocs);
				abort();
			}
		}
	
		if (!mapsInitialized) return RealX::malloc (sz);

		pid_t tid = gettid ();

		//If already there, then just pass the malloc to RealX
		bool find;
		if (activeThreads.find (tid, &find)) return RealX::malloc (sz);

		//Add thread id to list of active threads
		activeThreads.insertThread (tid, true);

		//Collect allocation data
		void* objAlloc;
		uint64_t before = rdtscp ();
		objAlloc = RealX::malloc(sz);
		uint64_t after = rdtscp ();
		uint64_t cyclesForMalloc = after - before;
		uint64_t address = reinterpret_cast <uint64_t> (objAlloc);

		//Has this address been used before
		if (addressUsage.find (address, &find)) {
			mallocLock.lock ();
			reused_address++;
			cyclesReuseAlloc += cyclesForMalloc;
			mallocLock.unlock ();
		}

		else {
			mallocLock.lock ();
			new_address ++;
			cyclesNewAlloc += cyclesForMalloc;
			mallocLock.unlock ();
			addressUsage.insertIfAbsent (address, 1);
		}
		
		mallocLock.lock ();
		numMallocs++;
		mallocLock.unlock ();

		//Remove thread id from active threads
		activeThreads.eraseThread (tid);
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
		// only call RealX::free() if the object did not come from here.
		if((ptr >= (void *) tmpbuf) &&
			(ptr <= (void *)(tmpbuf + TEMP_MEM_SIZE))) {
			numTempAllocs--;
			if(numTempAllocs == 0) {
					tmppos = 0;
			}
		} else {

			uint64_t before = rdtscp ();
			RealX::free(ptr);
			uint64_t after = rdtscp ();
			freeLock.lock ();
			numFrees ++;
			freeLock.unlock ();
			cyclesFree += (after - before);
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
		if(mallocInitialized != INITIALIZED) {
			if(ptr == NULL)
				return yymalloc(sz);
			yyfree(ptr);
			return yymalloc(sz);
		}

		reallocLock.lock ();
		inRealloc = true;
		void * reptr = RealX::realloc(ptr, sz);
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

	/*
	// Intercept thread creation
	int pthread_create(pthread_t * tid, const pthread_attr_t * attr,
			void *(*start_routine)(void *), void * arg) {
		return xthread::thread_create(tid, attr, start_routine, arg);
	}
	*/
	
	int pthread_mutex_lock(pthread_mutex_t *mutex) {

		if (!mapsInitialized) 
			return RealX::pthread_mutex_lock (mutex);

		uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);
		pid_t tid = gettid ();

		//Is this thread doing allocation
		bool find;
		if (activeThreads.find(tid, &find)) {

			//Have we encountered this lock before?
			LC* thisLock;
			if (lockUsage.find (lockAddr, &thisLock)) {
				thisLock->contention++;
				if (thisLock->contention > thisLock->maxContention) 
					thisLock->maxContention = thisLock->contention;
			}	

			//Add lock to lockUsage hashmap
			else {
				num_pthread_mutex_locks++;
				LC* lc = newLC();
				lockUsage.insertIfAbsent (lockAddr, lc);
			}	
		}

		//Aquire the lock
		int result = RealX::pthread_mutex_lock (mutex);
		return result;
	}

	int pthread_mutex_trylock (pthread_mutex_t *mutex) {

		if (!mapsInitialized)
			return RealX::pthread_mutex_trylock (mutex);
	
		uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);
		pid_t tid = gettid ();

		//Is this thread doing allocation
		bool find;
		if (activeThreads.find(tid, &find)) {

			//Have we encountered this lock before?
			LC* thisLock;
			if (lockUsage.find (lockAddr, &thisLock)) {
				thisLock->contention++;
				if (thisLock->contention > thisLock->maxContention) 
					thisLock->maxContention = thisLock->contention;
			}	

			//Add lock to lockUsage hashmap
			else {
				num_pthread_mutex_locks++;
				LC* lc = newLC();
				lockUsage.insertIfAbsent (lockAddr, lc);
			}	
		}

		//Try to aquire the lock
		int result = RealX::pthread_mutex_trylock (mutex);
		if (result != 0) trylockAttempts++;
		return result;
	}

	int pthread_mutex_unlock(pthread_mutex_t *mutex) {

		if (!mapsInitialized)
			return RealX::pthread_mutex_unlock (mutex);

		uint64_t lockAddr = reinterpret_cast <uint64_t> (mutex);
			
		//Decrement contention on this LC if this lock is
		//in our map
		LC* thisLock;
		if (lockUsage.find (lockAddr, &thisLock)) {
			thisLock->contention--;
		}

		return RealX::pthread_mutex_unlock (mutex);
	}
}

// Richard
// Create tuple for hashmap
ObjectTuple* newObjectTuple (int numAllocs, size_t objectSize) {

	ObjectTuple* objectTuple = (ObjectTuple*) RealX::malloc (sizeof (ObjectTuple));	
	objectTuple->numAllocs = numAllocs;
	objectTuple->size = objectSize;
	return objectTuple;
}

void* myMalloc (size_t size) {

	myMallocLock.lock ();
	void* retptr;
	if((tmppos + size) < TEMP_MEM_SIZE) {
		retptr = (void *)(tmpbuf + tmppos);
		tmppos += size;
		numTempAllocs++;
	} else {
		fprintf(stderr, "error: global allocator out of memory\n");
		fprintf(stderr, "\t requested size = %zu, total size = %d, "
							 "total allocs = %u\n", size, TEMP_MEM_SIZE*4, numTempAllocs);
		abort();
	}
	myMallocLock.unlock ();
	return retptr;
}

void myFree (void* ptr) {

	if (ptr == NULL) return;	

	myMallocLock.lock();
	if ((ptr >= tmpbuf) &&
		(ptr <= (tmpbuf + TEMP_MEM_SIZE))) {
		numTempAllocs--;
		if(numTempAllocs == 0) tmppos = 0;
	}
	myMallocLock.unlock();
}

// Try to figure out which allocator is being used
void getAllocStyle () {

	void* add1 = RealX::malloc (128);
	void* add2 = RealX::malloc (2048);

	long address1 = reinterpret_cast<long> (add1);
	long address2 = reinterpret_cast<long> (add2);

	long address1Page = address1 / 4096;
	long address2Page = address2 / 4096;

	if ((address1Page - address2Page) != 0) {

		bibop = true;
		fprintf (thrData.output, ">>> allocator     bibop\n");
	}

	else {

		bumpPointer = true;
		fprintf (thrData.output, ">>> allocator     bump-pointer\n");
	}
}

void getClassSizes () {

	std::vector <uint64_t> classSizes;
        size_t bytesToRead = 0;
        bool matchFound = false;        
        char *line;
        char *token;
        
        char *name_without_path = strrchr(program_invocation_name, '/') + 1;        
        
        classSizeFile = fopen(CLASS_SIZE_FILE_NAME, "a+");        
        
        while(getline(&line, &bytesToRead, classSizeFile) != -1){
            
                line[strcspn(line, "\n")] = 0;
                
                if(strcmp(line, name_without_path) == 0){
                    
                        matchFound = true;
                }
                
                if(matchFound) break;
        }
        
        if(matchFound){
                getline(&line, &bytesToRead, classSizeFile);
                line[strcspn(line, "\n")] = 0;
                
                while( (token = strsep(&line, " ")) ){
                    
                        if(atoi(token) != 0){
                        
                               classSizes.push_back( (uint64_t) atoi(token));
                        }
                }
        }
        
        if(!matchFound){
            
                void* oldAddress;
                void* newAddress;
                long oldAddr = 0, newAddr = 0;
                size_t oldSize = 8, newSize = 8;

                oldAddress = RealX::malloc (oldSize);
                oldAddr = reinterpret_cast <long> (oldAddress);

                while (oldSize <= MAX_CLASS_SIZE) {

                        newSize += 8;
                        newAddress = RealX::realloc (oldAddress, newSize);
                        newAddr = reinterpret_cast <long> (newAddress);

                        if ((newAddr != oldAddr))
                                classSizes.push_back (oldSize);

                        oldAddr = newAddr;
                        oldAddress = newAddress;
                        oldSize = newSize;
                }

                fprintf(classSizeFile, "%s\n", name_without_path);

                for (auto cSize = classSizes.begin (); cSize != classSizes.end (); cSize++) {
                        fprintf (classSizeFile, "%zu ", *cSize);
                }

                fprintf(classSizeFile, "\n");
                fclose(classSizeFile);
        }


	fprintf (thrData.output, ">>> classSizes    ");

	if (!classSizes.empty ()) {

		for (auto cSize = classSizes.begin (); cSize != classSizes.end (); cSize++) 
			fprintf (thrData.output, "%zu ", *cSize);
	}

	fprintf (thrData.output, "\n");
}

void getMmapThreshold () {

	inGetMmapThreshold = true;
	size_t size = 3000;
	void* mallocPtr;
	void* reallocPtr;

	// Find realloc mmap threshold
	usingRealloc = true;
	reallocPtr = RealX::malloc (size);
	for (int i = 0; i < 150000; i++) {

		reallocPtr = RealX::realloc (reallocPtr, size);
		size += 8;
	}
	usingRealloc = false;

	size = 3000;
	usingMalloc = true;
	// Find malloc mmap threshold
	for (int i = 0; i < 150000; i++) {

		mallocPtr = RealX::malloc (size);
		RealX::free (mallocPtr);
		size += 8;
	}
	usingMalloc = false;

	inGetMmapThreshold = false;
}

void writeAllocData () {

	fprintf (thrData.output, ">>> mallocs            %d\n", numMallocs);
	fprintf (thrData.output, ">>> new_address        %d\n", new_address);
	fprintf (thrData.output, ">>> reused_address     %d\n", reused_address);
	fprintf (thrData.output, ">>> frees              %d\n", numFrees);
	fprintf (thrData.output, ">>> num_mmaps          %d\n", num_mmaps);
	fprintf (thrData.output, ">>> malloc_mmap_threshold   %zu\n", malloc_mmap_threshold);
	fprintf (thrData.output, ">>> realloc_mmap_threshold  %zu\n", realloc_mmap_threshold);

	if (new_address > 0)
		fprintf (thrData.output, ">>> cyclesNewAlloc     %zu\n",
					(cyclesNewAlloc / (long)new_address));

	else
		fprintf (thrData.output, ">>> cyclesNewAlloc     N/A\n");

	if (reused_address > 0) 
		fprintf (thrData.output, ">>> cyclesReuseAlloc   %zu\n",
					(cyclesReuseAlloc / (long)reused_address));

	else 
		fprintf (thrData.output, ">>> cyclesReuseAlloc   N/A\n");

	if (numFrees > 0)
		fprintf (thrData.output, ">>> cyclesFree         %zu\n",
					(cyclesFree / (long)numFrees));

	else
		fprintf (thrData.output, ">>> cyclesFree         N/A\n");

	fprintf (thrData.output, ">>> pthread_mutex_lock %d\n", num_pthread_mutex_locks);
	fflush (thrData.output);
}

void writeContention () {
	pid_t pid = getpid ();
	char outputFile[MAX_FILENAME_LEN];
	snprintf(outputFile, MAX_FILENAME_LEN, "%s_contention_pid_%d.txt",
		program_invocation_name, pid);
	FILE* file = fopen (outputFile, "w");

	auto mapEnd = lockUsage.end();
	for (auto lock = lockUsage.begin(); lock != mapEnd; lock++) 
		fprintf (file, "lockAddr= %zu  maxContention= %d\n", lock.getkey(), lock.getData()->maxContention);

	fflush (file);
	fclose (file);
}

inline bool isAllocatorInCallStack() {
		void * array[256];
		int frames = backtrace(array, 256);
		int allocatorLevel = -2;

		//char buf[256];

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
	
      if(initializer() == IN_PROGRESS){
         return RealX::mmap(addr, length, prot, flags, fd, offset);
      }

		if (!mapsInitialized) 
			return RealX::mmap (addr, length, prot, flags, fd, offset);
	
		void* retval = RealX::mmap(addr, length, prot, flags, fd, offset);

		//Is the profiler getting mmap threshold?
		if (inGetMmapThreshold) {

			if ((realloc_mmap_threshold == 0) && usingRealloc)
				realloc_mmap_threshold = length;
			else if ((malloc_mmap_threshold == 0) && usingMalloc)
				malloc_mmap_threshold = length;
		}

		else {
			//Is this thread currently doing an allocation?
			pid_t tid = gettid ();
			bool find;
			if (activeThreads.find(tid, &find)) 
				mmap_lock.lock();
				num_mmaps++;
				mmap_lock.unlock();
		}

		return retval;
}
