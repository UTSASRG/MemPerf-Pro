#if !defined(_XMEMORY_H)
#define _XMEMORY_H

/*
 * @file   xmemory.h
 * @brief  Memory management for all.
 * @author Tongping Liu <http://www.cs.utsa.edu/~tongpingliu>
 */

#include <assert.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include <new>

#include "xdefines.hh"
#include "xheap.hh"
#include "xoneheap.hh"
#include "xpheap.hh"
#include "xthread.hh"

// Include all of heaplayers
#include "heaplayers.h"

// Encapsulates all memory spaces (globals & heap).
class xmemory {
private:
  // Private on purpose. See getInstance(), below.
  xmemory() {}

public:
  // Just one accessor.  Why? We don't want more than one (singleton)
  // and we want access to it neatly encapsulated here, for use by the
  // signal handler.
  static xmemory& getInstance() {
    static char buf[sizeof(xmemory)];
    static xmemory* theOneTrueObject = new (buf) xmemory();
    return *theOneTrueObject;
  }

  void initialize() {
    // Install a handler to intercept SEGV signals (used for trapping initial reads and
    // writes to pages).
    // Call _pheap so that xheap.h can be initialized at first and then can work normally.
    _heapBegin =
        (intptr_t)_pheap.initialize((void*)xdefines::USER_HEAP_BASE, xdefines::USER_HEAP_SIZE);

    _heapEnd = _heapBegin + xdefines::USER_HEAP_SIZE;
  }

  void finalize() {
    _pheap.finalize();
  }

  /* Heap-related functions. */
  inline void* malloc(size_t sz) {
    void* ptr = NULL;

	  ptr = realmalloc(sz);
    //  PRINT("malloc, current %p ptr %p sz %ld\n", current, ptr, sz);
    return ptr;
  }

  inline void* realloc(void* ptr, size_t sz) {
		if (ptr == NULL) {
    	ptr = malloc(sz);
    	return ptr;
  	}

  	if (sz == 0) {
    	free(ptr);
    	// 0 size = free. We return a small object.  This behavior is
    	// apparently required under Mac OS X and optional under POSIX.
    	return malloc(1);
		}

		// Now let's check the object size of this object
		objectHeader* o = getObject(ptr);

    // Get the block size
		size_t objSize = o->getObjectSize();

		// Change the size of object to the new address
		o->setObjectSize(sz);

  	void * buf = malloc(sz);

  	if (buf != NULL) {
    	// Copy the contents of the original object
    	// up to the size of the new block.
    	size_t minSize = (objSize < sz) ? objSize : sz;
    	memcpy (buf, ptr, minSize);
  	}

  	// Free the old block.
  	free(ptr);

  	return buf;
  }

  // Actual allocations
  inline void* realmalloc(size_t sz) {
    unsigned char* ptr = NULL;
   	size_t mysize = sz;

    if(sz == 0) {
			sz = 1;
    }
		
		// Align the object size, which should be multiple of 16 bytes.
    if(sz < 16) {
      mysize = 16;
    }
		mysize = (mysize + 15) & ~15;

    ptr = (unsigned char*)_pheap.malloc(mysize);
    objectHeader* o = getObject(ptr);

    // Set actual size there.
    o->setObjectSize(sz);

    //   PRINT("***********malloc object from %p to %lx. sz %lx\n", ptr, (unsigned long)ptr + sz, sz);
    return ptr;
  }

  // We are trying to find the aligned address starting from ptr
  // to ptr+boundary.
  inline void* getAlignedAddress(void* ptr, size_t boundary) {
    return ((intptr_t)ptr % boundary == 0)
               ? ptr
               : ((void*)(((intptr_t)ptr + boundary) & (~(boundary - 1))));
  }

  inline void* memalign(size_t boundary, size_t sz) {
    // Actually, malloc is easy. Just have more memory at first.
    void* ptr = malloc(boundary + sz);

    // Since the returned ptr is not aligned, return next aligned address
    void* newptr = getAlignedAddress(ptr, boundary);

    // check offset between newptr and ptr
    size_t offset = (intptr_t)newptr - (intptr_t)ptr;
    if(offset == 0) {
      newptr = (void*)((intptr_t)newptr + boundary);
      offset = boundary;
    }

    // Check whether the offset is valid?
    assert(offset >= 2 * sizeof(size_t));

    // Put the offset before the sentinel too
    void** origptr = (void**)((intptr_t)newptr - 2 * sizeof(size_t));
    *origptr = ptr;

    return newptr;
  }

  void* getObjectPtrAtFree(void* ptr) {
    size_t* prevPtr = (size_t*)((intptr_t)ptr - sizeof(size_t));
    void* origptr = ptr;

    if(*prevPtr == xdefines::MEMALIGN_SENTINEL_WORD) {
      void** ppPtr = (void**)((intptr_t)ptr - 2 * sizeof(size_t));
      origptr = *ppPtr;
    }
    return origptr;
  }

  bool inRange(intptr_t addr) { return (addr > _heapBegin && addr < _heapEnd) ? true : false; }

  // We should mark this whole objects with
  // some canary words.
  // Change the free operation to put into the tail of
  // list.
  void free(void* ptr) {
    void* origptr;

    if(!inRange((intptr_t)ptr)) {
      return;
    }

    // Check whether this is an memaligned object.
    origptr = getObjectPtrAtFree(ptr);
    objectHeader* o = getObject(origptr);

    _pheap.free(origptr);

    // We remove the actual size of this object to set free on an object.
    o->setObjectFree();
  }

  /// @return the allocated size of a dynamically-allocated object.
  inline size_t getSize(void* ptr) {
    // Just pass the pointer along to the heap.
    return _pheap.getSize(ptr);
  }

  inline void* getHeapEnd() { return _pheap.getHeapEnd(); }

  inline void* getHeapBegin() { return (void*)_heapBegin; }


  objectHeader* getObjectHeader(void* ptr) {
    objectHeader* o = (objectHeader*)ptr;
    return (o - 1);
  }

  // EDB: why is this here? Looks like a copy-paste bug (see above).
  static objectHeader* getObject(void* ptr) {
    objectHeader* o = (objectHeader*)ptr;
    return (o - 1);
  }

  void realfree(void* ptr);

private:
  intptr_t _heapBegin;
  intptr_t _heapEnd;

  /// The protected heap used to satisfy small objects requirement. Less than 256 bytes now.
  static xpheap<xoneheap<xheap>> _pheap;
};

#endif
