#define relaxed std::memory_order_relaxed
#define acquire std::memory_order_acquire
#define release std::memory_order_release
#define CALLSITE_MAXIMUM_LENGTH 20
#define ENTRY_SIZE 8
//Keys to bump-pointer freelist hashmap.
//512 is a large object for GlibC, and
//will be placed on a large object freelist.
//Hopefully other bp allocators are similar
#define LARGE_OBJECT 512
#define SMALL_OBJECT 0
//Bump-pointer key to overhead hashmap
#define BP_OVERHEAD 0
#define PAGE_BITS 12 
#define TEMP_MEM_SIZE 1024 * 1024 * 1024

//Structures
typedef struct {
	uint64_t addr;
	size_t size;
} FreeObject;

typedef struct {
	uint64_t addr;
	uint64_t numAccesses;
	size_t szTotal;
	size_t szFreed;
	size_t szUsed;
	uint64_t numAllocs;
	uint64_t numFrees;
} ObjectTuple;

typedef struct {
	uint64_t start;
	uint64_t end;
	size_t length;
	uint64_t rw;
	char origin;
	pid_t tid;
	std::atomic_uint allocations;
} MmapTuple;

typedef struct {
	size_t VmSize_start;
	size_t VmSize_end;
	size_t VmPeak;
	size_t VmRSS_start;
	size_t VmRSS_end;
	size_t VmHWM;
	size_t VmLib;
} VmInfo;

typedef struct {
	uint64_t faults = 0;
	uint64_t tlb_misses = 0;
	uint64_t cache_misses = 0;
	uint64_t cache_refs = 0;
	uint64_t instructions = 0;
} PerfReadInfo;

typedef struct LockContention {
	int contention;
	int maxContention;
} LC;

struct stack_frame {
	struct stack_frame * prev;	// pointing to previous stack_frame
	void * caller_address;		// the address of caller
};

//Enumerations
enum initStatus{
    INIT_ERROR = -1,
    NOT_INITIALIZED = 0, 
    IN_PROGRESS = 1, 
    INITIALIZED = 2
};

// Functions 
bool mappingEditor (void* addr, size_t len, int prot);
inline bool isAllocatorInCallStack();
inline size_t getClassSizeFor(size_t size);
int num_used_pages(uintptr_t vstart, uintptr_t vend);
int find_page(uintptr_t vstart, uintptr_t vend);
void analyzePerfInfo(PerfReadInfo*, PerfReadInfo*, size_t, bool*, pid_t);
void analyzeAllocation(size_t size, uint64_t address, uint64_t cycles, size_t, bool*);
void analyzeFree(uint64_t);
void calculateMemOverhead ();
void clearFreelists();
void doBefore(PerfReadInfo*, uint64_t*);
void doAfter(PerfReadInfo*, uint64_t*);
void get_bp_metadata();
void get_bibop_metadata();
void getAddressUsage(size_t size, uint64_t address, size_t classSize, uint64_t cycles);
void getAllocStyle ();
void getAlignment(size_t size, size_t classSize);
void getBlowup(size_t size, uint64_t address, size_t classSize, bool*);
void getClassSizes ();
void getMappingsUsage(size_t size, uint64_t address, size_t classSize);
void getMemUsageStart ();
void getMemUsageEnd ();
void getMetadata(size_t classSize);
void getMmapThreshold ();
void getOverhead(size_t size, uint64_t address, size_t classSize, bool*);
void getPerfInfo(PerfReadInfo*);
void globalizeThreadAllocData();
void increaseMemoryHWM(size_t size);
void myFree (void* ptr);
void writeAllocData ();
void writeContention ();
void writeMappings();
void writeOverhead();
void writeThreadMaps();
void writeAddressUsage ();
void* myMalloc (size_t size);
unsigned search_vpage (uintptr_t vpage);
FreeObject* newFreeObject (uint64_t addr, uint64_t size);
LC* newLC ();
MmapTuple* newMmapTuple (uint64_t address, size_t length, int prot, char origin);
ObjectTuple* newObjectTuple (uint64_t address, size_t size);
thread_alloc_data* newTad();
