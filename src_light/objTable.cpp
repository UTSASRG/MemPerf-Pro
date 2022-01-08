
#include "objTable.h"

extern HashMap <void*, ObjStat, PrivateHeap> objStatusMap;
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

    bool reused;
//    uint8_t callkey = Callsite::getCallKey(0);
//    hashLocksSet.lock(address);
    ObjStat * status = objStatusMap.find(address, sizeof(unsigned long));
    if(status == nullptr) {
        hashLocksSet.lock(address);
        status = objStatusMap.insert(address, sizeof(unsigned long), ObjStat::newObj(Callsite::getCallKey(0), ThreadLocalStatus::runningThreadIndex, size));
        hashLocksSet.unlock(address);
        status->size = size;
        reused = false;
    } else {
        status->callKey = Callsite::getCallKey(status->callKey);
        status->size = size;
        status->tid = ThreadLocalStatus::runningThreadIndex;
        reused = true;
    }

    return reused;
}


ObjStat* ObjTable::freeUpdate(void* address) {
    ObjStat * status = objStatusMap.find(address, sizeof(void *));
    return status;
}
