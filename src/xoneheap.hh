#if !defined(DOUBLETAKE_XONEHEAP_H)
#define DOUBLETAKE_XONEHEAP_H

#include <stdint.h>

#include <new>

/**
 * @class xoneheap
 * @brief Wraps a single heap instance.
 *
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

template <class SourceHeap> class xoneheap {
public:
  enum { Alignment = 16 };

  void* initialize(void* start, size_t size, size_t metasize) {
    return getHeap()->initialize(start, size, metasize);
  }
  // Get heap start and end, this is used to check range.
  void* getHeapStart() { return getHeap()->getHeapStart(); }
  void* getHeapEnd() { return getHeap()->getHeapEnd(); }
  void* getHeapPosition() { return getHeap()->getHeapPosition(); }

  void* malloc(size_t sz) { return getHeap()->malloc(sz); }
  void free(void* ptr) { getHeap()->free(ptr); }
  size_t getSize(void* ptr) { return getHeap()->getSize(ptr); }

private:
  SourceHeap* getHeap() {
    static char heapbuf[sizeof(SourceHeap)];
    static SourceHeap* _heap = new (heapbuf) SourceHeap;
    //   fprintf (stderr, "xoneheap at %p\n", _heap);
    return _heap;
  }
};

#endif
