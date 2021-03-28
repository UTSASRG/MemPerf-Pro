#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

  static void* allocate(bool isShared, size_t sz, int fd, void* startaddr) {
    int protInfo = PROT_READ | PROT_WRITE;
    int sharedInfo = isShared ? MAP_SHARED : MAP_PRIVATE;
    sharedInfo |= ((fd == -1) ? MAP_ANONYMOUS : 0);
    sharedInfo |= ((startaddr != (void*)0) ? MAP_FIXED : 0);
    sharedInfo |= MAP_NORESERVE;

    void* ptr = mmap(startaddr, sz, protInfo, sharedInfo, fd, 0);
    if(ptr == MAP_FAILED) {
      fprintf(stderr, "Couldn't do mmap (%s) : startaddr %p, sz %lx, protInfo=%d, sharedInfo=%d\n",
            strerror(errno), startaddr, sz, protInfo, sharedInfo);
      exit(-1);
    } else {
      //fprintf (stderr, "Successful map %lu with ptr %p.\n", sz, ptr);
    }
    return ptr;
  }
  
  void* mmapAllocatePrivate(size_t sz, void* startaddr = NULL, int fd = -1) {
    return allocate(false, sz, fd, startaddr);
  }

#define PAGE_SIZE 4096
int main() {
	int i, j;
	char * ptr;

  for(i = 0; i < 10000; i++) {
     ptr = (char *)mmapAllocatePrivate(PAGE_SIZE);
     *ptr = 'A';
     //munmap(ptr, PAGE_SIZE);
     //ptr = (char *)mmapAllocatePrivate(PAGE_SIZE);
     madvise(ptr, PAGE_SIZE, MADV_DONTNEED);
     *ptr = 'A';
  }

	return EXIT_SUCCESS;
}
