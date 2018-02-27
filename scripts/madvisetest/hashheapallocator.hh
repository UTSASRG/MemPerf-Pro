#ifndef __HASHHEAPALLOCATOR_HH__
#define __HASHHEAPALLOCATOR_HH__

#include "real.hh"

class HeapAllocator {
public:
  static void* allocate(size_t sz) {
    void* ptr = Real::malloc(sz);
    if(!ptr) {
      return NULL;
    }
    return ptr;
  }

  static void deallocate(void* ptr) {
    Real::free(ptr);
    ptr = NULL;
  }
};
#endif
