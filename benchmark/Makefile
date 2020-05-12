TARGET = micro-tcmalloc micro-jemalloc cpu_freq

CXX = g++
CXXFLAGS = -g -O2 -std=c++14 -Wno-unused-result -Wl,--no-as-needed -DCYCLES_PERSECOND=2094895684.64 -DCYCLES_PER_PAUSE=5.0

TCMALLOC_LIB = /usr/local/lib/libtcmalloc_minimal.so
JEMALLOC_LIB = /media/umass/datasystem/xin/allocaters/jemalloc-5.2.1/lib/libjemalloc.so


all: $(TARGET)

micro-tcmalloc: micro-benchmark.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) -rdynamic ${TCMALLOC_LIB} micro-benchmark.cpp -o micro-tcmalloc -lpthread -ldl

micro-jemalloc: micro-benchmark.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) -rdynamic ${JEMALLOC_LIB} micro-benchmark.cpp -o micro-jemalloc -lpthread -ldl

cpu_freq: cpu_freq_test.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) cpu_freq_test.cpp -o cpu_freq -lpthread

clean:
	rm -f $(TARGET)
