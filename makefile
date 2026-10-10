# BombConsole: `make` builds the server, `make run` starts it, `make check` runs
# a headless round with two bots.
PYTHON ?= python3

all: server/A_server

server/A_server: server/*.cpp server/*.hpp server/makefile
	$(MAKE) -C server

run: server/A_server
	./server/A_server $(ARGS)

check: server/A_server
	$(PYTHON) tools/check.py -- $(ARGS)

clean:
	$(MAKE) -C server clean

.PHONY: all run check clean
