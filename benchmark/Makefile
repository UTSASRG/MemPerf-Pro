TARGET = micro

CXX = g++
CXXXFLAGS= -g -O2 -std=c++14 -Wno-unused-result
$(TARGET): micro-benchmark.cpp
        $(CXX) -std=c++14 $(CXXFLAGS) micro-benchmark.cpp -o $(TARGET) -lpthread

clean:
        rm -f $(TARGET)