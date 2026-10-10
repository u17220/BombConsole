# BombConsole server. Needs only g++ and make; ncurses is optional (auto-detected).
ifeq ($(origin CXX),default)
CXX := g++
endif
CXXFLAGS := -O2 -std=c++11 -Wall -Wextra
SRCS := server.cpp Config.cpp Game.cpp ServerClient.cpp Ui.cpp
OBJS := $(SRCS:%.cpp=%.o)

# ncurses detection: try ncursesw, then ncurses; otherwise build without a screen
NCURSES_LIB := $(shell for l in ncursesw ncurses; do \
	if $(CXX) ncurses_probe.cpp -l$$l -o /dev/null >/dev/null 2>&1; then echo -l$$l; break; fi; done)
ifeq ($(NCURSES_LIB),)
CPPFLAGS += -DNO_CURSES
$(info ncurses not found: building without the server screen (headless only))
endif
LDFLAGS := $(NCURSES_LIB) -lpthread

all: A_server

A_server: $(OBJS)
	$(CXX) -o $@ $(CXXFLAGS) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $<

clean:
	rm -f *.o *.d A_server

.PHONY: all clean

-include $(wildcard *.d)
