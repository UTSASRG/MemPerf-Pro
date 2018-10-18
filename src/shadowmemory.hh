#ifndef __SHADOWMAP_H__
#define __SHADOWMAP_H__

#include <cstddef>

class ShadowMemory {
    private:

        inline size_t alignup(size_t size, size_t alignto) {
             return (size % alignto == 0) ? size : ((size + (alignto - 1)) & ~(alignto - 1));
        }

        inline int key(size_t size);

        const static short PAGESIZE = 4096;
        const static short CACHEBLOCK = 64;
        const static short WASTEMASK = 0x3F;

        size_t waste;

        size_t startAddr;
        size_t endAddr;

        size_t realStartAddr;
        size_t realEndAddr;

        int *cacheline;

    public:
        ShadowMemory();

        void initialize(size_t address, size_t size);

        size_t getAddress() { return startAddr; }

        size_t getSize() { return endAddr - startAddr; }

        void update(size_t address, size_t size);
};

#endif // __SHADOWMAP_H__
