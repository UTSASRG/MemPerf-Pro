TARGET = micro cpu_freq

CXX = g++
CXXXFLAGS= -g -O2 -std=c++14 -Wno-unused-result
<<<<<<< HEAD
all: $(TARGET)

micro: micro-benchmark.cpp
    $(CXX) -std=c++14 $(CXXFLAGS) micro-benchmark.cpp -o micro -lpthread

cpu_freq: cpu_freq_test.cpp
    $(CXX) -std=c++14 $(CXXFLAGS) cpu_freq_test.cpp -o cpu_freq -lpthread

clean:
	rm -f $(TARGET)
