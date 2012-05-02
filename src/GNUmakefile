CC = $(CROSS)gcc
LD = $(CROSS)gcc

CFLAGS = -Wall -std=gnu99 -I. -Os -g0 -march=i686 -ffast-math -fexcess-precision=fast
LDFLAGS = -shared -Wl,--dll,--add-stdcall-alias -Wl,-s -L.

.PHONY: all clean

all: avsreader.aui

avsreader.aui: avsreader.o d2v_parser.o
	$(LD) $(LDFLAGS) $(XLDFLAGS) -o $@ $^

avsreader.o: avsreader.c input.h avisynth_c.h
	$(CC) -c $(CFLAGS) $(XCFLAGS) $< -o $@

d2v_parser.o: d2v_parser.c
	$(CC) -c $(CFLAGS) $(XCFLAGS) $< -o $@

clean:
	$(RM) *.aui *.o
