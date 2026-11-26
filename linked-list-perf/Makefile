CC = gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic

.PHONY: all clean
all: list-perf

list-perf: bench.c list.c list.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fno-lto bench.c list.c $(LDFLAGS) -o $@ $(LDLIBS)

clean:
	$(RM) list-perf
