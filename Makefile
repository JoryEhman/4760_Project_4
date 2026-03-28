# Makefile — CS4760 Assignment 4: Process Scheduling
# Author: student
# Date:   2026-03-27

CC      = gcc
CFLAGS  = -Wall -Wextra -g
TARGETS = oss worker

all: $(TARGETS)

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $<

oss: oss.o
	$(CC) $(CFLAGS) -o oss oss.o

worker: worker.o
	$(CC) $(CFLAGS) -o worker worker.o

# Dependencies
oss.o:    oss.c shared.h queue.h
worker.o: worker.c shared.h

# Remove object files and executables
clean:
	rm -f *.o $(TARGETS)

# Also remove log files
cleanall: clean
	rm -f *.log

.PHONY: all clean cleanall
