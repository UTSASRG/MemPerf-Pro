TARGET = micro-tcmalloc micro-jemalloc cpu_freq allocation_base_test allocation_base_test-tcmalloc allocation_base_test-jemalloc system_call_overheads_test

CXX = g++
CXXFLAGS = -g -O2 -std=c++14 -Wno-unused-result -Wl,--no-as-needed -DCYCLES_PERSECOND=2094895684.64 -DCYCLES_PER_PAUSE=5.0

TCMALLOC_LIB = /usr/local/lib/libtcmalloc_minimal.so
#JEMALLOC_LIB_DIR = /home/tliu/Memoryallocators/jemalloc/lib/
JEMALLOC_LIB_DIR = /media/umass/datasystem/xin/allocaters/jemalloc-5.2.1/lib/
JEMALLOC_LIB = libjemalloc.so


all: $(TARGET)

micro-tcmalloc: micro-benchmark.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) -rdynamic ${TCMALLOC_LIB} micro-benchmark.cpp -o micro-tcmalloc -lpthread -ldl

system_call_overheads_test: system_call_overheads_test.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) system_call_overheads_test.cpp -o system_call_overheads_test -lpthread -ldl

micro-jemalloc: micro-benchmark.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) -rdynamic ${JEMALLOC_LIB_DIR}${JEMALLOC_LIB} micro-benchmark.cpp -o micro-jemalloc -lpthread -ldl

cpu_freq: cpu_freq_test.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) cpu_freq_test.cpp -o cpu_freq -lpthread

allocation_base_test-tcmalloc: allocation_base_test.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) -rdynamic ${TCMALLOC_LIB} allocation_base_test.cpp -o allocation_base_test-tcmalloc -lpthread

allocation_base_test-jemalloc: export LD_LIBRARY_PATH=${JEMALLOC_LIB_DIR}
allocation_base_test-jemalloc: allocation_base_test.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) -rdynamic ${JEMALLOC_LIB_DIR}${JEMALLOC_LIB} allocation_base_test.cpp -o allocation_base_test-jemalloc -lpthread

clean:
	rm -f $(TARGET)
