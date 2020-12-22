#include "selfmap.hh"

#ifdef PRINT_LEAK_OBJECTS

regioninfo regions[MAX_REGION_NUM];
uint8_t numOfRegion;
regioninfo silentRegions[MAX_REGION_NUM];
uint8_t numOfSilentRegions;
bool selfmapInit;

#endif

