all: A_server
CPPFLAGS :=-O3 -std=c++11 -Wall
ifeq ($(shell uname -s),Darwin)
# macOS: libncurses is already wide-character capable (no libncursesw)
LDFLAGS :=-lncurses -lpthread
else
LDFLAGS :=-lncursesw -lpthread
endif
SRCS := SquareObject.cpp Block.cpp Player.cpp Bomb.cpp Fire.cpp ServerClient.cpp
OBJS := $(SRCS:%.cpp=%.o)
DEPS := $(SRCS:%.cpp=%.d)

A_server: server.o $(OBJS)
		g++ -o $@ $(CPPFLAGS) server.o $(OBJS) $(LDFLAGS)

test: test.o $(OBJS)
		g++ -o $@ $(CPPFLAGS)  test.o $(OBJS) $(LDFLAGS)

test1: test1.o $(OBJS)
		g++ -o $@ $(CPPFLAGS)  test1.o $(OBJS) $(LDFLAGS)

test2: test2.o $(OBJS)
		g++ -o $@ $(CPPFLAGS)  test2.o $(OBJS) $(LDFLAGS)

test3: test3.o $(OBJS)
		g++ -o $@ $(CPPFLAGS)  test3.o $(OBJS) $(LDFLAGS)


.cpp.o:
	g++ -MMD -MP -c $<

run: a.out
		./a.out

clean:
		rm -f *.o *.d

-include $(DEPS)
