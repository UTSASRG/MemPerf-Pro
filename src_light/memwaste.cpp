
#include "memwaste.h"

extern HashMap<void*, uint32_t, PrivateHeap> objStatusMap;
HashLocksSet MemoryWaste::hashLocksSet;

void HashLocksSet::lock(void *address) {
    size_t hashKey = HashFuncs::hashAddr(address, sizeof(void*)) & (MAX_OBJ_NUM-1);
    locks[hashKey].lock();
}

void HashLocksSet::unlock(void *address) {
    size_t hashKey = HashFuncs::hashAddr(address, sizeof(void*)) & (MAX_OBJ_NUM-1);
    locks[hashKey].unlock();
}

void MemoryWaste::initialize() {
    objStatusMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);
}


bool MemoryWaste::allocUpdate(unsigned int size, void * address) {
    bool reused;
    hashLocksSet.lock(address);
    uint32_t * status = objStatusMap.find(address, sizeof(unsigned long));
    if(!status) {
        status = objStatusMap.insert(address, sizeof(unsigned long), size);
        hashLocksSet.unlock(address);
        reused = false;
    }
    else {
        *status = size;
        hashLocksSet.unlock(address);
        reused = true;
    }

    return reused;
}


uint32_t MemoryWaste::freeUpdate(void* address) {

    hashLocksSet.lock(address);
    uint32_t * status = objStatusMap.find(address, sizeof(void *));
    if(status == nullptr) {
        hashLocksSet.unlock(address);
        return 0;
    }
    hashLocksSet.unlock(address);
    return *status;
}
