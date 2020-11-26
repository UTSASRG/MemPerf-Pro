#if !defined(DOUBLETAKE_SELFMAP_H)
#define DOUBLETAKE_SELFMAP_H

/*
 * @file   selfmap.h
 * @brief  Process the /proc/self/map file.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <functional>
#include <map>
#include <new>
#include <string>
#include <utility>

#include "definevalues.h"

using namespace std;



struct regioninfo {
  void* start;
  void* end;
};

/**
 * A single mapping parsed from the /proc/self/maps file
 */
class mapping {
public:
  mapping() : _valid(false) {}

  mapping(uintptr_t base,
	  uintptr_t limit,
	  char* perms,
	  size_t offset,
	  std::string file)
      : _valid(true),
	_base(base),
	_limit(limit),
	_readable(perms[0] == 'r'),
        _writable(perms[1] == 'w'),
	_executable(perms[2] == 'x'),
	_copy_on_write(perms[3] == 'p'),
        _offset(offset),
	_file(file) {}

  bool valid() const { return _valid; }

  bool isText() const { return _readable && !_writable && _executable; }

  bool isStack() const { return _file == "[stack]"; }

  bool isGlobals(std::string mainfile) const {
    // global mappings are RW_P, and either the heap, or the mapping is backed
    // by a file (and all files have absolute paths)
    // the file is the current executable file, with [heap], or with lib*.so
    // Actually, the mainfile can be longer if it has some parameters.
    return (_readable && _writable && !_executable && _copy_on_write) &&
      (_file.size() > 0 && (_file == mainfile ||  _file == "[heap]" || _file.find(".so") != std::string::npos));
  }

  uintptr_t getBase() const { return _base; }

  uintptr_t getLimit() const { return _limit; }

  const std::string& getFile() const { return _file; }

private:
  bool _valid;
  uintptr_t _base;
  uintptr_t _limit;
  bool _readable;
  bool _writable;
  bool _executable;
  bool _copy_on_write;
  size_t _offset;
  std::string _file;
};

/// Read a mapping from a file input stream
static std::ifstream& operator>>(std::ifstream& f, mapping& m) {
  if(f.good() && !f.eof()) {
    uintptr_t base, limit;
    char perms[5];
    size_t offset;
    size_t dev_major, dev_minor;
    int inode;
    string path;

    // Skip over whitespace
    f >> std::skipws;

    // Read in "<base>-<limit> <perms> <offset> <dev_major>:<dev_minor> <inode>"
    f >> std::hex >> base;
    if(f.get() != '-')
      return f;
    f >> std::hex >> limit;

    if(f.get() != ' ')
      return f;
    f.get(perms, 5);

    f >> std::hex >> offset;
    f >> std::hex >> dev_major;
    if(f.get() != ':')
      return f;
    f >> std::hex >> dev_minor;
    f >> std::dec >> inode;

    // Skip over spaces and tabs
    while(f.peek() == ' ' || f.peek() == '\t') {
      f.ignore(1);
    }

    // Read out the mapped file's path
    getline(f, path);

    m = mapping(base, limit, perms, offset, path);
  }

  return f;
}

extern mapping maps[MAX_REGION_NUM];
extern unsigned short numOfMaps;
extern regioninfo regions[MAX_REGION_NUM];
extern unsigned short numOfRegion;

class selfmap {
public:
  static selfmap& getInstance() {
    static char buf[sizeof(selfmap)];
    static selfmap* theOneTrueObject = new (buf) selfmap();
    return *theOneTrueObject;
  }

  /// Check whether an address is inside the DoubleTake library itself.
  bool isDoubleTakeLibrary(void* pcaddr) {
    return ((pcaddr >= _doubletakeStart) && (pcaddr <= _doubletakeEnd));
  }

  /// Check whether an address is inside the main application.
  bool isApplication(void* pcaddr) {
    return ((pcaddr >= _appTextStart) && (pcaddr <= _appTextEnd));
  }

  // Print out the code information about an eip address.
  // Also try to print out the stack trace of given pcaddr.
  void printCallStack();
  void printCallStack(int depth, void** array);
  static int getCallStack(void** array);

  /// Collect all global regions.
  void getGlobalRegions() {
    size_t index = 0;
      numOfRegion = 0;
    for(unsigned short i = 0; i < numOfMaps; ++i) {
      mapping m = maps[i];

      if(m.isGlobals(_main_exe) && m.getFile().find("libmallocprof") == std::string::npos) {
//        if(m.isGlobals(_main_exe) ) {
//        fprintf(stderr, "getGlobalRegiions: m.getBase() %lx m.getLimit() %lx isglobals and added %s\n", m.getBase(), m.getLimit(), m.getFile().c_str());
        regions[index].start = (void*)m.getBase();
        regions[index].end = (void*)m.getLimit();
        index++;
      }
    }
  }

  selfmap() {
      bool gotMainExe = false;
    ifstream maps_file("/proc/self/maps");
    numOfMaps = 0;
    while(maps_file.good() && !maps_file.eof()) {
      mapping m;
      maps_file >> m;
			if(!gotMainExe) {
				_main_exe = std::string(m.getFile());
				gotMainExe = true;
			} 

      if(m.valid()) {
          maps[numOfMaps++] = m;
      }
    }
  }

    std::string _main_exe;
  void* _appTextStart;
  void* _appTextEnd;
  void* _doubletakeStart;
  void* _doubletakeEnd;
};



#endif
