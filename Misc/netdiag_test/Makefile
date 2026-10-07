# netgraph collector tests  // woods #netgraph
#   make -C Misc/netdiag_test        build and run
CC ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined

all: run

test_netdiag: test_netdiag.c ../../Quake/netgraph.c ../../Quake/netgraph.h
	$(CC) $(CFLAGS) -DNETGRAPH_STANDALONE -o $@ test_netdiag.c ../../Quake/netgraph.c -lm

run: test_netdiag
	./test_netdiag

clean:
	rm -f test_netdiag

.PHONY: all run clean
