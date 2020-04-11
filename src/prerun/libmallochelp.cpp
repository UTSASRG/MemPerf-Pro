/*
	libmallocprof helper library
	The exectuable must be ran with sudo to access /proc/pid/pagemap
	This will create a file for libmallocprof to use
	Run these executables from the directory that the application will be
	tested from
	
	If you are going to prerun with OBSD don't forget to export
	MALLOC_OPTIONS=p and run with sudo -E 
*/
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/types.h>
#include "../libmallocprof.h"
#include "../real.hh"
#include "../hashmap.hh"
#include "../hashfuncs.hh"
#include "../spinlock.hh"
#include "../selfmap.hh"

#define BUFFER_SIZE 4096000 * 4
#define PAGE_SIZE 4096

//Library Information Globals
bool inAllocation = false;
bool inGetAllocStyle = false;
bool lookingForLargeObject = false;
bool inMalloc = false;
bool inMmap = false;
bool inThread = false;
bool mmap_called = false;
bool sbrk_called = false;
bool bibop = false;
bool bump_point = false;
bool inGetClassSizes = false;
bool libInitialized;
bool mapsInitialized = false;
bool realInitialized = false;
pid_t pid;
short nextFreeClassIndex = 0;
size_t class_sizes[200];
size_t large_object_threshold = 0;
size_t metadata_object = 0;
size_t thread_mmap_threshold;
size_t thread_sbrk_threshold;
unsigned total_mmaps = 0;
char* allocator_name;
FILE* outputFile;

//Unused
//__thread thread_data thrData;
bool opening_maps_file;
bool isLibc;

//Debugging flags DEBUG
const bool d_mmap = false;
const bool d_bp_metadata = false;
const bool d_bibop_metadata = false;
const bool d_style = false;
const bool d_getClassSizes = false;
const bool d_constructor = false;
const bool d_search_vpage = false;

//Temp Allocator Globals
char myBuffer [BUFFER_SIZE];
unsigned long temp_position = 0;
unsigned long temp_allocations = 0;

typedef int (*main_fn_t)(int, char **, char **);
main_fn_t real_main;

void exitHandler();
void printFromGlobal(char*);
void writeClassSizes();
void getAllocStyle();
void get_bp_metadata();
unsigned search_vpage (uintptr_t vpage);
int find_pages (uintptr_t vstart, uintptr_t vend, unsigned long pagesFound[]);
void* find_large_object(void*);
void* find_class_sizes(void*);

//spinlock temp_mem_lock;

//Aliases
extern "C" {

	// Function aliases
	void free(void *) __attribute__ ((weak, alias("yyfree")));
	void * calloc(size_t, size_t) __attribute__ ((weak, alias("yycalloc")));
	void * malloc(size_t) __attribute__ ((weak, alias("yymalloc")));
	void * realloc(void *, size_t) __attribute__ ((weak, alias("yyrealloc")));
	void * mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) __attribute__ ((weak, alias("yymmap")));
}

void initReal() {
	if (!realInitialized) {
		RealX::initializer();
	}
}

//Constructor
__attribute__((constructor)) void libmallochelp_initializer () {
	if (libInitialized) return;
	allocator_name = (char*)myMalloc(128);
	initReal();
	//temp_mem_lock.init();
	pid = getpid();
	libInitialized = true;
}

//Finalizer
__attribute__((destructor)) void libmallochelp_finalizer () {

}

//Helper Main
int helper_main (int argc, char ** argv, char ** envp) {

	// Register our cleanup routine as an on-exit handler.
	atexit(exitHandler);
	
	// Determine allocator style
	getAllocStyle();

	fprintf(stderr, "Allocator style is %s\n", bibop ? "bibop" : "bump-point");

	//The thread will find a large object threshold. This will be
	//determined by either mmap or sbrk being called during malloc
	pthread_t worker;
	void* result;
	int create, join;

	fprintf(stderr, "Creating a thread to find large object threshold\n");
	create = pthread_create(&worker, NULL, &find_large_object, nullptr);
	if (create != 0) fprintf(stderr, "Error creating thread\n");
	join = pthread_join(worker, &result);
	if (join != 0) fprintf(stderr, "Error joining thread\n");
	else fprintf(stderr, "Thread has joined\n");

	if (!large_object_threshold) {
		fprintf(stderr, "Couldn't find a large_object_threshold. Abort()\n");
		abort();
	}
	else fprintf(stderr, "large_object_threshold = %zu\n", large_object_threshold);

	if (bibop) {
	
		fprintf(stderr, "Creating a thread to find class sizes\n");
		create = pthread_create(&worker, NULL, &find_class_sizes, nullptr);
		if (create != 0) fprintf(stderr, "Error creating thread\n");
		join = pthread_join(worker, &result);
		if (join != 0) fprintf(stderr, "Error joining thread\n");
		else fprintf(stderr, "Thread has joined\n");
	}
	else get_bp_metadata();

	selfmap::getInstance().getTextRegions();
	
	char* end_path = strrchr(allocator_name, '/');
	char* end_name = strchr(end_path, '.') - 1;
	char* start_name = end_path + 1;
	uint64_t name_bytes = (uint64_t)end_name - (uint64_t)end_path;
	uint64_t path_bytes = (uint64_t)start_name - (uint64_t)allocator_name;
	short ext_bytes = 6;
	char filename[128];
	void* r;

	r = memcpy(filename, allocator_name, path_bytes);
	if (r != filename) {perror("memcpy fail?"); abort();}
	r = memcpy((filename+path_bytes), start_name, name_bytes);
	if (r != (filename+path_bytes)) {perror("memcpy fail?"); abort();}
	snprintf ((filename+path_bytes+name_bytes), ext_bytes, ".info");

	outputFile = fopen(filename, "w");
	if (outputFile == NULL) {
		perror("Failed to open \".info\" file");
		abort();
	}
	fprintf(stderr, "The \".info\" file was created here --> %s\n", filename);

	return real_main (argc, argv, envp);
}

void exitHandler() {

	//Write .info file
	fprintf(stderr, "Writing to .info file\n");
	fprintf (outputFile, "style %s\n", bibop ? "bibop" : "bump_pointer");
	if (bibop) writeClassSizes();
	fprintf (outputFile, "large_object_threshold %zu\n", large_object_threshold);
	fflush(outputFile);
	fclose(outputFile);
	fprintf(stderr, "Prerun finished. Exiting\n");
}

extern "C" int __libc_start_main(main_fn_t, int, char **, void (*)(),
		void (*)(), void (*)(), void *) __attribute__((weak,
			alias("helper_libc_start_main")));

extern "C" int helper_libc_start_main(main_fn_t main_fn, int argc,
		char ** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(),
		void * stack_end) {
	auto real_libc_start_main =
		(decltype(__libc_start_main) *)dlsym(RTLD_NEXT, "__libc_start_main");
	real_main = main_fn;
	return real_libc_start_main(helper_main, argc, argv, init, fini,
			rtld_fini, stack_end);
}

// Memory management functions
extern "C" {
	// MALLOC
	void* yymalloc(size_t size) {

		if(!libInitialized) {
			if((temp_position + size) < BUFFER_SIZE)
				return myMalloc (size);
			else {
				fprintf(stderr, "error: temp allocator out of memory\n");
				fprintf(stderr, "\t requested size = %zu, total size = %d, total allocs = %lu\n",
						  size, BUFFER_SIZE, temp_allocations);
				abort();
			}
		}

		if (inMalloc) return RealX::malloc(size);

		inMalloc = true;
		inAllocation = true;

		//Do allocation
		void* p = RealX::malloc(size);

		inMalloc = false;
		inAllocation = false;
		return p;
	}

	// CALLOC
	void * yycalloc(size_t nelem, size_t elsize) {

		if (!libInitialized) {
			void * ptr = NULL;
			ptr = yymalloc (nelem * elsize);
			if (ptr) memset(ptr, 0, nelem * elsize);
			return ptr;
		}

		inAllocation = true;

		// Do allocation
		void* object = RealX::calloc(nelem, elsize);

		inAllocation = false;
		return object;
	}

	// FREE
	void yyfree(void * ptr) {
		if(ptr == NULL) return;

		// Determine whether the specified object came from our global buffer;
		// only call RealX::free() if the object did not come from here.
		if ((ptr >= (void *) myBuffer) && (ptr <= ((void *)(myBuffer + BUFFER_SIZE)))) {
			myFree (ptr);
			return;
		}

		//Do free
		RealX::free(ptr);
	}

	// REALLOC
	void * yyrealloc(void * ptr, size_t size) {

		if (!libInitialized) {
			if(ptr == NULL) return yymalloc(size);
			yyfree(ptr);
			return yymalloc(size);
		}

		inAllocation = true;

		//Do allocation
		void* object = RealX::realloc(ptr, size);

		inAllocation = false;
		return object;
	}

	// MMAP
	void * yymmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {

		if (inMmap) return RealX::mmap (addr, length, prot, flags, fd, offset);
		if (!libInitialized) libmallochelp_initializer();

		inMmap = true;

		if (inThread) mmap_called = true;

		void* p = RealX::mmap(addr, length, prot, flags, fd, offset);

		total_mmaps++;

		inMmap = false;
		return p;
	}

	// SBRK
    void *sbrk(intptr_t increment){

		if (!libInitialized) libmallochelp_initializer();

		if (inThread) sbrk_called = true;

        void *retptr = RealX::sbrk(increment);

        return retptr;
    }
}//End of extern "C"

void* myMalloc (size_t size) {

	void* p;
	if((temp_position + size) < TEMP_MEM_SIZE) {
		p = (void *)(myBuffer + temp_position);
		temp_position += size;
		temp_allocations++;
	} else {
		fprintf(stderr, "error: global allocator out of memory\n");
		fprintf(stderr, "\t requested size = %zu, total size = %d, "
						"total allocs = %lu\n", size, TEMP_MEM_SIZE, temp_allocations);
		abort();
	}
	return p;
}

void myFree (void* ptr) {

	if (ptr == NULL) return;
	if ((ptr >= (void*)myBuffer) && (ptr <= ((void*)(myBuffer + TEMP_MEM_SIZE)))) {
		temp_allocations--;
		if (temp_allocations == 0) temp_position = 0;
	}
}

// Try to figure out which allocator is being used
void getAllocStyle () {

	inGetAllocStyle = true;

	//Hopefully this is first malloc to initialize the allocator
	void* p1 = RealX::malloc(64);
	if (d_style) printf ("p1=%p\n", p1);

	void* p2 = RealX::malloc (256);
	if (d_style) printf ("p2=%p\n", p2);

	uint64_t addr1 = (uint64_t) p1;
	uint64_t addr2 = (uint64_t) p2;

	RealX::free (p1);
	RealX::free (p2);

	uint64_t address1Page = addr1 / PAGE_SIZE;
	uint64_t address2Page = addr2 / PAGE_SIZE;

	if ((address1Page - address2Page) != 0) bibop = true;
	else bump_point = true;

	if (d_style) printf ("Style is %s\n", bibop ? "bibop" : "bump point");

	inGetAllocStyle = false;
}

/*
	Assuming the metadata is placed with the object

	Get metadata size for bump pointer allocator
	Allocate two objects. Start with a metadata size of
	the difference between addresses of the objects.
	Realloc the first object using 1 byte incremenets, and
	decrement the metadata by 1 byte. Once realloc moves
	the object, you should be left with a correct metadata
	size
*/
void get_bp_metadata() {
	size_t size = 16;
	size_t metadata = 0;

	void* ptr1 = RealX::malloc(size);
	void* ptr2 = RealX::malloc(size);
	void* realloc_addr;

	if (d_bp_metadata) printf ("ptr1= %p, ptr2= %p\n", ptr1, ptr2);

	uint64_t first = (uint64_t) ptr1;
	uint64_t second = (uint64_t) ptr2;

	uint64_t diff = (second - first);
	metadata = (diff - size);

	if (d_bp_metadata) printf ("first= %#lx, second= %#lx, diff= %zu\n", first, second, diff);
	if (d_bp_metadata) printf ("starting metadata size= %zu\n", metadata);
	
	do {
		size++;
		realloc_addr = RealX::realloc(ptr1, size);
		if (realloc_addr != ptr1) {
			if (d_bp_metadata) printf ("object moved. Break loop\n");
			break;
		}
		if (d_bp_metadata) {
			printf ("size = %zu\n", size);
			printf ("diff = %lu\n", diff);
		}
		metadata--;
	} while (realloc_addr == ptr1);

	if (d_bp_metadata) printf ("metadata= %zu\n", metadata);

	RealX::free(ptr2);
	RealX::free(realloc_addr);
	metadata_object = metadata;
}

/*
Read in 8 bytes at a time on this vpage and compare it
with 0. Looking for "used" bytes. If the value is anything
other than 0, consider it used.
*/
unsigned search_vpage (uintptr_t vpage) {

	unsigned bytes = 0;
	uint64_t zero = 0;
	uint64_t word;
	for (unsigned offset = 0; offset <= 4088; offset += 8) {

		word = *((uint64_t*) (vpage + offset));
		if ((word | zero) > 0) {
			bytes += 8;
			bool true_initialized = libInitialized;
			libInitialized = false;
			if (d_search_vpage) printf ("hexVal=%#lx\n", word);
			libInitialized = true_initialized;
		}
	}
	return bytes;
}

/*
 * Returns the number of virtual pages that are backed by physical frames
 * in the given mapping (note that this is not the same thing as the number
 * of distinct physical pages used within the mapping, as we do not account
 * for the possibility of multiply-used frames).
 *
 * Returns -1 on error, or the number of pages on success.
 */
int num_used_pages(uintptr_t vstart, uintptr_t vend) {
	char pagemap_filename[50];
	snprintf (pagemap_filename, 50, "/proc/%d/pagemap", pid);
	int fdmap;
	uint64_t bitmap;
	unsigned long pagenum_start, pagenum_end;
	unsigned num_pages_read = 0;
	unsigned num_pages_to_read = 0;
	unsigned num_used_pages = 0;

	if((fdmap = open(pagemap_filename, O_RDONLY)) == -1) {
		return -1;
	}

	pagenum_start = vstart >> PAGE_BITS;
	pagenum_end = vend >> PAGE_BITS;
	num_pages_to_read = pagenum_end - pagenum_start + 1;
	if(num_pages_to_read == 0) {
		close(fdmap);
		return -1;
	}

	if(lseek(fdmap, (pagenum_start * ENTRY_SIZE), SEEK_SET) == -1) {
		close(fdmap);
		return -1;
	}

	do {
		if(read(fdmap, &bitmap, ENTRY_SIZE) != ENTRY_SIZE) {
			close(fdmap);
			return -1;
		}

		num_pages_read++;
		if((bitmap >> 63) == 1) {
			num_used_pages++;
		}
	} while(num_pages_read < num_pages_to_read);

	close(fdmap);
	return num_used_pages;
}

/* 
	Read 8 byte increments from the pagemap file looking for
	virtual pages that have a physical page backing.

	vstart:			starting virtual page
	vend:			ending virtual page
	pagesFound[]:	array to place the "page index" into this mmap region
					where a physically backed page was found
	
	return			the number of vpages with physical backing
*/
int find_pages (uintptr_t vstart, uintptr_t vend, unsigned long pagesFound[]) {
	char pagemap_filename[50];
	snprintf (pagemap_filename, 50, "/proc/%d/pagemap", pid);
	int fdmap;
	uint64_t bitmap;
	unsigned long pagenum_start, pagenum_end;
	uint64_t num_pages_read = 0;
	uint64_t num_pages_to_read = 0;
	unsigned current_page = 0;

	if((fdmap = open(pagemap_filename, O_RDONLY)) == -1) {
		perror ("failed to open pagemap_filename");
		return -1;
	}

	pagenum_start = vstart >> PAGE_BITS;
	pagenum_end = vend >> PAGE_BITS;
	num_pages_to_read = pagenum_end - pagenum_start + 1;
	if(num_pages_to_read == 0) {
		fprintf (stderr, "num_pages_to_read == 0\n");
		close(fdmap);
		return -1;
	}

	if(lseek(fdmap, (pagenum_start * ENTRY_SIZE), SEEK_SET) == -1) {
		perror ("failed to lseek on file");
		close(fdmap);
		return -1;
	}

	unsigned entries = 0;
	do {
		if(read(fdmap, &bitmap, ENTRY_SIZE) != ENTRY_SIZE) {
			perror ("failed to read 8 bytes");
			close(fdmap);
			return -1;
		}

		num_pages_read++;
		if((bitmap >> 63) == 1) {
			//Save the pagenumber
			pagesFound[entries] = current_page;
			entries++;
		}
		current_page++;
	} while(num_pages_read < num_pages_to_read);

	close(fdmap);
	return entries;
}

MmapTuple* newMmapTuple (uint64_t address, size_t length, int prot, char origin) {

	MmapTuple* t = (MmapTuple*) myMalloc (sizeof (MmapTuple));

	uint64_t end = (address + length) - 1;
	t->start = address;
	t->end = end;
	t->length = length;
	t->rw = 0;
	t->origin = origin;
	if (prot == (PROT_READ | PROT_WRITE)) t->rw += length;
	else if (prot == (PROT_READ | PROT_WRITE | PROT_EXEC)) t->rw += length;
	t->tid = pid;
	t->allocations = 0;
	return t;
}

// First integer after class_sizes is how many there will be
void writeClassSizes() {
	
	fprintf (outputFile, "class_sizes %d ", nextFreeClassIndex);
	for (int i = 0; i < nextFreeClassIndex; i++) {
		fprintf (outputFile, "%zu ", class_sizes[i]);
	}
	fprintf (outputFile, "\n");
}

void* find_large_object (void* arg) {

	uint64_t tid = pthread_self();
	fprintf(stderr, "[%lu] has been created. Working..\n", tid);

	//Create the thread heap
	void* mallocPtr = RealX::malloc(0);
	RealX::free(mallocPtr);

	size_t size = 4088;
	inThread = true;
	mmap_called = false;
	sbrk_called = false;

	// Find large object threshold
	while (!mmap_called && !sbrk_called && (size < MAX_CLASS_SIZE)) {

		size += 8;
		mallocPtr = RealX::malloc (size);
		RealX::free (mallocPtr);
	}

	if (mmap_called) {
		large_object_threshold = size;
		fprintf(stderr, "[%lu] mmap has been called\n", tid);
	}
	else if (sbrk_called) {
		large_object_threshold = size;
		fprintf(stderr, "[%lu] sbrk has been called\n", tid);
	}

	inThread = false;
	return nullptr;
}

void* find_class_sizes (void* arg) {

	uint64_t tid = pthread_self();
	fprintf(stderr, "[%lu] has been created. Working..\n", tid);

	//Create the thread heap
	void* mallocPtr = RealX::malloc(0);
	RealX::free(mallocPtr);

	void* oldPointer;
	void* newPointer;
	size_t oldSize = 8, newSize = 8;

	oldPointer = RealX::malloc (oldSize);

	// If the object moves, save the oldSize as a class size
	while (oldSize <= large_object_threshold) {

		newSize += 8;
		newPointer = RealX::realloc (oldPointer, newSize);
		if (newPointer != oldPointer) {
			//Save the new class Size
			class_sizes[nextFreeClassIndex] = oldSize;
			nextFreeClassIndex++;
		}
		oldPointer = newPointer;
		oldSize = newSize;
	}
    if (class_sizes[nextFreeClassIndex-1] != large_object_threshold) {
        class_sizes[nextFreeClassIndex] = large_object_threshold;
        nextFreeClassIndex++;
    }
	RealX::free (newPointer);
	return nullptr;
}
