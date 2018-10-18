#include "shadowmemory.hh"
#include <malloc.h>
#include "real.hh"

#include <iostream>
#include <sys/mman.h>

ShadowMemory::ShadowMemory() {}

void ShadowMemory::initialize(size_t address, size_t size) {
    waste = 0;

    startAddr = alignup(address, PAGESIZE);
    endAddr = startAddr + size;

    realStartAddr = address;
    realEndAddr = address + size;

    cacheline = (int *)RealX::mmap(NULL, size, PROT_READ | PROT_WRITE,
                                           MAP_ANON | MAP_PRIVATE, -1, 0);
    std::cout << "ShadowMemory(address=" << std::hex << address << ", range=<" << startAddr << "," << endAddr << ">)" << std::endl;
}

inline int ShadowMemory::key(size_t size) {
    int remainder = size % CACHEBLOCK;
    int waste = (remainder != 0) ? CACHEBLOCK - remainder : 0;
    return (size << 6) | (0xFF & waste);
}

void ShadowMemory::update(size_t address, size_t size) {
    int metadata = key(size);
    // int key = (startAddr - address) >> 6;
    int key = (address - startAddr) >> 6;
    cacheline[key] = metadata;
}
