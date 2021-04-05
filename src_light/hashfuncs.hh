#ifndef DOUBLETAKE_HASHFUNCS_H
#define DOUBLETAKE_HASHFUNCS_H

/*
 * @file   hashfuncs.h
 * @brief  Some functions related to hash table.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include <stdint.h>
#include <string.h>
#include <stdint.h>

class HashFuncs {
public:

  static size_t hash8Int(uint8_t x, size_t) { return x; }
    static size_t hash32Int(uint32_t x, size_t) { return x; }
  static size_t hash64Int(uint64_t x, size_t) { return x; }

  static size_t hashAddr(void* addr, size_t) {
      return (unsigned long)addr >> 7;
//    unsigned long key = addr;
//    key ^= (key << 15) ^ 0xcd7dcd7d;
//    key ^= (key >> 10);
//    key ^= (key << 3);
//    key ^= (key >> 6);
//    key ^= (key << 2) + (key << 14);
//    key ^= (key >> 16);
//    return key;
  }

  static bool compare8Int(uint8_t var1, uint8_t var2, size_t) { return var1 == var2; }
    static bool compare32Int(uint32_t var1, uint32_t var2, size_t) { return var1 == var2; }
    static bool compare64Int(uint64_t var1, uint64_t var2, size_t) { return var1 == var2; }

  static bool compareAddr(void* addr1, void* addr2, size_t) { return addr1 == addr2; }
};

#endif
