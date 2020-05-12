TARGET = micro

CXX = g++

$(TARGET): micro-benchmark.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) micro-benchmark.cpp -o $(TARGET) -lpthread

clean:
	rm -f $(TARGET)
