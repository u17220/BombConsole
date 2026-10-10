all: A_server
CXXFLAGS :=-O3 -std=c++11 -Wall
ifeq ($(shell uname -s),Darwin)
# macOS: libncurses is already wide-character capable (no libncursesw)
LDFLAGS :=-lncurses -lpthread
else
LDFLAGS :=-lncursesw -lpthread
endif
SRCS := SquareObject.cpp Block.cpp Player.cpp Bomb.cpp Fire.cpp ServerClient.cpp
OBJS := $(SRCS:%.cpp=%.o)
TESTS := test test1 test2 test3 sctest

A_server: server.o $(OBJS)
		g++ -o $@ $(CXXFLAGS) server.o $(OBJS) $(LDFLAGS)

test: test.o $(OBJS)
		g++ -o $@ $(CXXFLAGS)  test.o $(OBJS) $(LDFLAGS)

test1: test1.o $(OBJS)
		g++ -o $@ $(CXXFLAGS)  test1.o $(OBJS) $(LDFLAGS)

test2: test2.o $(OBJS)
		g++ -o $@ $(CXXFLAGS)  test2.o $(OBJS) $(LDFLAGS)

test3: test3.o $(OBJS)
		g++ -o $@ $(CXXFLAGS)  test3.o $(OBJS) $(LDFLAGS)

sctest: sctest.o ServerClient.o
		g++ -o $@ $(CXXFLAGS)  sctest.o ServerClient.o $(LDFLAGS)

%.o: %.cpp
	g++ $(CXXFLAGS) -MMD -MP -c $<

clean:
		rm -f *.o *.d A_server $(TESTS)

.PHONY: all clean

-include $(wildcard *.d)
