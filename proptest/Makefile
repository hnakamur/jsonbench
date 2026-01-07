CXX = g++
CXXFLAGS = -std=c++17 -Wall -I/usr/include -I/usr/src/googletest/googletest/include -I/usr/src/googletest/googlemock/include -I../src
LDFLAGS = -L/usr/lib/x86_64-linux-gnu -lrapidcheck -lyajl -pthread

GTEST_DIR = /usr/src/googletest/googletest
GTEST_SRCS = $(GTEST_DIR)/src/gtest-all.cc
GTEST_OBJS = gtest-all.o

# JSON parser objects from src directory
PARSER_OBJS = ../src/nlparser.o ../src/rjparser.o ../src/jsonbench-yajlparser.o

TARGET = proptest
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS) $(GTEST_OBJS) $(PARSER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(GTEST_OBJS): $(GTEST_SRCS)
	$(CXX) $(CXXFLAGS) -I$(GTEST_DIR) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) $(GTEST_OBJS)

.PHONY: all clean
