#ifndef __HASHFUNCS_HH__
#define __HASHFUNCS_HH__

#include <stdint.h>
#include <string.h>

class HashFuncs {
public:
  // The following functions are borrowed from the STL.
  static size_t hashString(char* start, size_t len) {
  //static size_t hashString(const void* start, size_t len) {
    unsigned long __h = 0;
    char* __s = start;
    //char* __s = (char*)start;
		int i = 0;
    //auto i = 0;

    for(; i <= (int) len; i++, ++__s)
      __h = 5 * __h + *__s;

    return size_t(__h);
  }

  static size_t hashInt(const int x, size_t) { return x; }

  static size_t hashLong(long x, size_t) { return x; }

  static size_t hashUnsignedlong(unsigned long x, size_t) { return x; }

  static size_t hashAddr(void* addr, size_t) {
    unsigned long key = (unsigned long)addr;
    key ^= (key << 15) ^ 0xcd7dcd7d;
    key ^= (key >> 10);
    key ^= (key << 3);
    key ^= (key >> 6);
    key ^= (key << 2) + (key << 14);
    key ^= (key >> 16);
    return key;
  }

	static size_t hashAddrs(intptr_t* addr, size_t len) {
		size_t result = ~0;
		for(size_t i = 0; i < len; i++) result ^= addr[i];
		return result;
	}

  static bool compareAddr(void* addr1, void* addr2, size_t) { return addr1 == addr2; }

  static bool compareInt(int var1, int var2, size_t) { return var1 == var2; }

  //static bool compareString(const char* str1, const char* str2, size_t len) {
  static bool compareString(char* str1, char* str2, size_t len) {
    return strncmp(str1, str2, len) == 0;
  }
};

#endif
