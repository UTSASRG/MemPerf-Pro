#ifndef __PRIVATE_HEAP__
#define __PRIVATE_HEAP__

extern "C" void * dlmalloc (size_t);
extern "C" void   dlfree (void *);
extern "C" size_t dlmalloc_usable_size (void *);

class PrivateHeap {
public:
  static void * allocate (size_t sz) {
    return dlmalloc (sz);
  }

  static void deallocate (void * ptr) {
    dlfree (ptr);
  }

  static size_t getSize (void * ptr) {
    return dlmalloc_usable_size (ptr);
  }

};


#endif
