#!/bin/make -f

CCFLAGS ?= -O0 -fsanitize

# PATH =+
CC = clang
CXX = clang++
LD = lld

%.o: %.c
	clang $(CCFLAGS) -Wall -c -o $@ $<

monitorctl: DDC.o
	clang $(CCFLAGS) -Wall -o $@ -lobjc -framework CoreGraphics  -framework IOKit -framework AppKit -framework Foundation $< $@.m

install: monitorctl
	install monitorctl /usr/local/bin

clean:
	-rm *.o monitorctl

update:
	-make clean
	-make DDC.o
	-make monitorctl
	-make install
	-monitorctl -b 90 -c 90

framebuffers:
	ioreg -c IOFramebuffer -k IOFBI2CInterfaceIDs -b -f -l -r -d 1

displaylist:
	ioreg -c IODisplayConnect -b -f -r -l -i -d 2

test: monitorctl
	-monitorctl -b 90 -c 90

all: clean DDC.o monitorctl

.PHONY: all clean install displaylist
