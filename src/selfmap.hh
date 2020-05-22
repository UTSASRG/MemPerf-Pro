#if !defined(DOUBLETAKE_SELFMAP_H)
#define DOUBLETAKE_SELFMAP_H

/*
 * @file   selfmap.h
 * @brief  Process the /proc/self/map file.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <functional>
#include <map>
#include <new>
#include <string>
#include <utility>

#include "interval.hh"
#include "real.hh"
#include "memsample.h"

#define LIBC_LIBRARY_NAME "libmalloc.so" 

// From heaplayers
//#include "wrappers/stlallocator.h"

using namespace std;

extern bool isLibc;
extern char *allocator_name;
//extern thread_local thread_data thrData;
extern bool opening_maps_file;

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

    mapping(uintptr_t base, uintptr_t limit, char* perms, size_t offset, std::string file)
      : _valid(true), _base(base), _limit(limit), _readable(perms[0] == 'r'),
      _writable(perms[1] == 'w'), _executable(perms[2] == 'x'), _copy_on_write(perms[3] == 'p'),
      _offset(offset), _file(file) {}

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

    //maybe it is global area
    bool isGlobalsExt() const {
      return _readable && _writable && !_executable && _copy_on_write && _file.size() == 0;
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

class selfmap {
  public:
    static selfmap& getInstance() {
      static char buf[sizeof(selfmap)];
      static selfmap* theOneTrueObject = new (buf) selfmap();
      return *theOneTrueObject;
    }

    std::string getApplicationName(){
      return _main_exe;
    }

    /// Check whether an address is inside the Current library itself.
    bool isCurrentLibrary(void* pcaddr) {
      return ((pcaddr >= _mallocProfTextStart) && (pcaddr <= _mallocProfTextEnd));
    }

    bool isAllocator(void* pcaddr) {
		if (!haveTextRegions) {
			getTextRegions(nullptr);
		}
      return ((pcaddr >= _allocTextStart) && (pcaddr <= _allocTextEnd));
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

    /// Get information about global regions.
	void getTextRegions(char * allocator_name) {
		for(const auto& entry : _mappings) {
			const mapping& m = entry.second;
			if(m.isText()) {
				if(m.getFile().find("/libmallocprof") != std::string::npos) {
					_mallocProfTextStart = (void*)m.getBase();
					_mallocProfTextEnd = (void*)m.getLimit();
					_currentLibrary = std::string(m.getFile());

				} else if(m.getFile() == _main_exe) {
					_appTextStart = (void*)m.getBase();
					_appTextEnd = (void*)m.getLimit();
				} else {
					uintptr_t mallocSymbol = (uintptr_t)RealX::malloc;
					uintptr_t libTextStart = (uintptr_t)m.getBase();
					uintptr_t libTextEnd = (uintptr_t)m.getLimit();
					_allocLibrary = std::string(m.getFile());
					if(mallocSymbol == (uintptr_t)NULL) { continue; }
					if((libTextStart <= mallocSymbol) && (mallocSymbol <= libTextEnd)) {
						_allocTextStart = (void *)libTextStart;
						_allocTextEnd = (void *)libTextEnd;
						isLibc = (strcasestr(_allocLibrary.c_str(), LIBC_LIBRARY_NAME) != NULL);
						strcpy (allocator_name, _allocLibrary.c_str());
					}
				}
			}
    	}
		haveTextRegions = true;
	}

  private:
    selfmap() {
      // Read the name of the main executable
      // char buffer[PATH_MAX];
      //RealX::readlink("/proc/self/exe", buffer, PATH_MAX);
      //_main_exe = std::string(buffer);
      bool gotMainExe = false;
      // Build the mappings data structure
	  opening_maps_file = true;
      ifstream maps_file("/proc/self/maps");
	  opening_maps_file = false;

		while(maps_file.good() && !maps_file.eof()) {
			mapping m;
			maps_file >> m;
			// It is more clean that that of using readlink. 
			// readlink will have some additional bytes after the executable file 
			// if there are parameters.	
			if(!gotMainExe) {
				_main_exe = std::string(m.getFile());
				gotMainExe = true;
			} 

			if(m.valid()) {
//				fprintf(stderr, "Base %lx limit %lx\n", m.getBase(), m.getLimit()); 
				_mappings[interval(m.getBase(), m.getLimit())] = m;
			}
		}
	}

	bool haveTextRegions = false;
	std::map<interval, mapping, std::less<interval>> _mappings;

	std::string _main_exe;
	std::string _currentLibrary;
	std::string _allocLibrary;
	void* _appTextStart;
	void* _appTextEnd;
	void * _allocTextStart;
	void * _allocTextEnd;
	void* _mallocProfTextStart;
	void* _mallocProfTextEnd;
};

#endif
