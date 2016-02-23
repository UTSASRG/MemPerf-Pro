#if !defined(DOUBLETAKE_XPHEAP_H)
#define DOUBLETAKE_XPHEAP_H

/*
 * @file   xpheap.h
 * @brief  A heap optimized to reduce the likelihood of false sharing.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include <assert.h>
#include <stddef.h>

#include <new>

#include "objectheader.hh"
#include "xdefines.hh"

// Include all of heaplayers
#include "heaplayers.h"

template <class SourceHeap> class AdaptAppHeap : public SourceHeap {

public:
  void* malloc(size_t sz) {
		void *ptr;

// We are adding one objectHeader and two "canary" words along the object
// The layout will be:  objectHeader + "canary" + Object + "canary".
    ptr = SourceHeap::malloc(sz + sizeof(objectHeader));
    if(!ptr) {
      return NULL;
    }

//	  fprintf(stderr, "AdaptAppHeap malloc sz %lx ptr %p\n", sz, ptr);
    // Set the objectHeader.
    objectHeader* o = new (ptr) objectHeader(sz);
    void* newptr = getPointer(o);

    assert(getSize(newptr) == sz);

    return newptr;
  }

  void free(void* ptr) { SourceHeap::free((void*)getObject(ptr)); }

  size_t getSize(void* ptr) {
    objectHeader* o = getObject(ptr);
    size_t sz = o->getSize();
    if(sz == 0) {
			fprintf(stderr, "Object size cannot be zero\n");
			abort();
		}
    return sz;
  }

private:
  static objectHeader* getObject(void* ptr) {
    objectHeader* o = (objectHeader*)ptr;
    return (o - 1);
  }

  static void* getPointer(objectHeader* o) { return (void*)(o + 1); }
};

template <class SourceHeap, int Chunky>
class KingsleyStyleHeap
    : public HL::ANSIWrapper<
          HL::StrictSegHeap<Kingsley::NUMBINS, Kingsley::size2Class, Kingsley::class2Size,
                            HL::AdaptHeap<HL::SLList, AdaptAppHeap<SourceHeap>>,
                            AdaptAppHeap<HL::ZoneHeap<SourceHeap, Chunky>>>> {
private:
  typedef HL::ANSIWrapper<
      HL::StrictSegHeap<Kingsley::NUMBINS, Kingsley::size2Class, Kingsley::class2Size,
                        HL::AdaptHeap<HL::SLList, AdaptAppHeap<SourceHeap>>,
                        AdaptAppHeap<HL::ZoneHeap<SourceHeap, Chunky>>>> SuperHeap;

public:
  KingsleyStyleHeap() {}

private:
  // We want that a single heap's metadata are on different page
  // to avoid conflicts on one page
  //  char buf[4096 - (sizeof(SuperHeap) % 4096)];
};

// Different processes will have a different heap.
// class PerThreadHeap : public TheHeapType {
template <int NumHeaps, class TheHeapType> class PerThreadHeap {
public:
  PerThreadHeap() {
    //  fprintf(stderr, "TheHeapType size is %ld\n", sizeof(TheHeapType));
  }

  void* malloc(int ind, size_t sz) {
    //    fprintf(stderr, "PerThreadheap malloc ind %d sz %d _heap[ind] %p\n", ind, sz, &_heap[ind]);
    // Try to get memory from the local heap first.
    void* ptr = _heap[ind].malloc(sz);
    return ptr;
  }

  // Here, we will give one block of memory back to the originated process related heap.
  void free(int ind, void* ptr) {
    _heap[ind].free(ptr);
    // fprintf(stderr, "now first word is %lx\n", *((unsigned long*)ptr));
  }

  // For getSize, it doesn't matter which heap is used
  // since they are the same
  size_t getSize(void* ptr) {
		//fprintf(stderr, "perthreadheap getSize %p\n", ptr);
		return _heap[0].getSize(ptr); 
	}

private:
  TheHeapType _heap[NumHeaps];
};

// Protect heap
template <class SourceHeap> class xpheap : public SourceHeap {
  typedef PerThreadHeap<xdefines::NUM_HEAPS,
                        KingsleyStyleHeap<SourceHeap, xdefines::USER_HEAP_CHUNK>> SuperHeap;
  // typedef PerThreadHeap<xdefines::NUM_HEAPS, KingsleyStyleHeap<SourceHeap,
  // AdaptAppHeap<SourceHeap>, xdefines::USER_HEAP_CHUNK> >

public:
  xpheap() {}

  void* initialize(void* start, size_t heapsize) {

    int metasize = alignup(sizeof(SuperHeap), xdefines::PageSize);

    // Initialize the SourceHeap before malloc from there.
    char* base = (char*)SourceHeap::initialize(start, heapsize, metasize);

    _heap = new (base) SuperHeap;

    // Get the heap start and heap end;
    _heapStart = SourceHeap::getHeapStart();
    _heapEnd = SourceHeap::getHeapEnd();

    return (void*)_heapStart;
  }

  void* getHeapEnd() { return (void*)SourceHeap::getHeapPosition(); }

  void* malloc(size_t size) {
    // printf("malloc in xpheap with size %d\n", size);
    return _heap->malloc(getThreadIndex(), size);
  }

  void free(void* ptr) {
    int tid = getThreadIndex();

    // If an object is too large, we simply freed this object.
    _heap->free(tid, ptr);
  }

  void realfree(void* ptr) { _heap->free(getThreadIndex(), ptr); }

  size_t getSize(void* ptr) { 
		//fprintf(stderr, "xheap getSize ptr %p\n", ptr);
		return _heap->getSize(ptr); 
	}

  bool inRange(void* addr) { return ((addr >= _heapStart) && (addr <= _heapEnd)) ? true : false; }

private:
  SuperHeap* _heap;
  void* _heapStart;
  void* _heapEnd;
};

#endif
