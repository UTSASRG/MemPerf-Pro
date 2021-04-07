
#include "objTable.h"

extern HashMap<void*, uint32_t, PrivateHeap> objStatusMap;
HashLocksSet ObjTable::hashLocksSet;

void HashLocksSet::lock(void *address) {
    size_t hashKey = HashFuncs::hashAddr(address, sizeof(void*)) & (MAX_OBJ_NUM-1);
    locks[hashKey].lock();
}

void HashLocksSet::unlock(void *address) {
    size_t hashKey = HashFuncs::hashAddr(address, sizeof(void*)) & (MAX_OBJ_NUM-1);
    locks[hashKey].unlock();
}

void ObjTable::initialize() {
    objStatusMap.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_OBJ_NUM);
}


bool ObjTable::allocUpdate(unsigned int size, void * address) {

//    return true;

    bool reused = true;

//    hashLocksSet.lock(address);
    uint32_t * status = objStatusMap.find(address, sizeof(unsigned long));
    if(status == nullptr) {
        hashLocksSet.lock(address);
        status = objStatusMap.findOrAdd(address, sizeof(unsigned long));
        hashLocksSet.unlock(address);
//        *status = size;
        reused = false;
    }
//    else {
//        hashLocksSet.unlock(address);
//    }

    *status = size;

    return reused;
}


uint32_t ObjTable::freeUpdate(void* address) {
//    return 8;

//    hashLocksSet.lock(address);
    uint32_t * status = objStatusMap.find(address, sizeof(void *));
    if(status == nullptr) {
//        hashLocksSet.unlock(address);
        return 0;
    }
//    hashLocksSet.unlock(address);
    return *status;
}
