
#include "objTable2.h"

extern HashMap<uint64_t, HashMap<uint32_t, uint32_t, PrivateHeap>, PrivateHeap> ObjTable::chunkTable;
extern HashLocksSet ObjTable::hashLocksSet;

void HashLocksSet::lock(uint64_t chunkId) {
    size_t hashKey = HashFuncs::hash64Int(chunkId, sizeof(uint64_t)) & (NUM_CHUNK-1);
    locks[hashKey].lock();
}

void HashLocksSet::unlock(uint64_t chunkId) {
    size_t hashKey = HashFuncs::hash64Int(chunkId, sizeof(uint64_t)) & (NUM_CHUNK-1);
    locks[hashKey].unlock();
}

void ObjTable::initialize() {
    chunkTable.initialize(HashFuncs::hash64Int, HashFuncs::compare64Int, NUM_CHUNK);
}

inline uint64_t ObjTable::getChunkId(void * addr) {
    return (uint64_t)addr >> LOG2_CHUNK;
}

inline uint32_t ObjTable::getChunkOffset(void * addr) {
    return (uint32_t) ((uint64_t)addr & MASK_CHUNK) >> 3;
}

bool ObjTable::allocUpdate(uint32_t size, void * address) {

    return true;

    uint64_t chunkId = getChunkId(address);
    uint32_t chunkOffset = getChunkOffset(address);
    bool reuse;

//    hashLocksSet.lock(chunkId);

    HashMap<uint32_t, uint32_t, PrivateHeap> * hash = chunkTable.find(chunkId, sizeof(uint64_t));
    if (hash == nullptr) {
        hashLocksSet.lock(chunkId);
        if (hash == nullptr) {
            hash = chunkTable.insert(chunkId, sizeof(uint64_t), HashMap<uint32_t, uint32_t, PrivateHeap>());
//            hash->initialize(HashFuncs::hash32Int, HashFuncs::compare32Int, MAX_CHUNK_OBJ);
        }
        hashLocksSet.unlock(chunkId);
    }


//    uint32_t * status = hash->find(chunkOffset, sizeof(uint32_t));
//    if(status == nullptr) {
//        hash->insert(chunkOffset, sizeof(uint32_t), size);
//        reuse = false;
//    } else {
//        *status = size;
//        reuse = true;
//    }


//    return reuse;
}


uint32_t ObjTable::freeUpdate(void* address) {
    return 8;
}
