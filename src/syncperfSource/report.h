#ifndef _REPORT_H_
#define _REPORT_H_

#include<iostream>
#include<fstream>
#include<sstream>
#include<string>
#include "recordentries.hh"
#include "mutex_manager.h"
#include<map>
#include<vector>
#include <stdexcept>

#define MAXBUFSIZE 4096

/*
 * @file   report.h
 * @brief  Reporting utilities for SyncPerf
 * @author Mejbah<mohammad.alam@utsa.edu>
 */


typedef struct {
	char line_info[MAX_NUM_STACKS][MAX_CALL_STACK_DEPTH * 50];
	double conflict_rate;
	double frequency;
	int count; //number of line info
}sync_perf_t;

typedef struct {
	UINT32 access_count;
	UINT32 fail_count;
	char call_site[MAX_CALL_STACK_DEPTH * 50];
}call_site_info_t;


class Report {

private:
	char _curFilename[MAXBUFSIZE];
	Report(){}
 	

public:
	static Report& getInstance() {
		static Report instance;
    return instance;		
	}	

	enum { THRESHOLD_CONFLICT = 5 };
	enum { THRESHOLD_FREQUENCY = 1 }; //per millisecond

	std::string exec(const char* cmd) {
		//std::cout << "exec CMD: " << cmd << std::endl; //TODO: remove this
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
			fprintf( stderr, "Could not pipe in exec function\n" );
			return "ERROR";
		}
    char buffer[128];
    std::string result = "";
    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != NULL){
//						printf("%s", buffer); //TODO: remove this
            result += buffer;
				}
    }
    pclose(pipe);
    return result;
  }

	void setFileName(){
		int count = readlink("/proc/self/exe", _curFilename, MAXBUFSIZE);
    if (count <= 0 || count >= MAXBUFSIZE)
    {
      fprintf(stderr, "Failed to get current executable file name\n" );
      exit(1);
    }
    _curFilename[count] = '\0';
	}

  std::string get_call_stack_string( long *call_stack ){
		
    char buf[MAXBUFSIZE];
    std::string stack_str="";

    int j=0;
    while(call_stack[j] != 0 ) {
      //printf("%#lx\n", m->stacks[i][j]);  
      sprintf(buf, "addr2line -e %s  -a 0x%lx  | tail -1", _curFilename, call_stack[j] );
      std::string source_line =  exec(buf);
      if(source_line[0] != '?') { // not found
        //get the file name only
        std::size_t found = source_line.find_last_of("/\\");
        source_line = source_line.substr(found+1);
        stack_str += source_line.erase(source_line.size()-1); // remove the newline at the end
        stack_str += " ";
      }
      j++;
    }
    return stack_str;
  }


	void print( RecordEntries<mutex_t>&sync_vars ){
	}

	void write_report( std::fstream& fs, std::vector<sync_perf_t>& results){
	}

	void report_quadrant( std::fstream& fs, sync_perf_t sync_perf_entry,int count){
	}	

};

#endif
