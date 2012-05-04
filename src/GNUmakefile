SRCS = avsreader.c d2v_parser.c
OBJS = $(SRCS:%.c=%.o)

CC = $(CROSS)gcc
LD = $(CROSS)gcc

CFLAGS = -Wall -std=gnu99 -I. -Os -g0 -march=i686 -ffast-math -fexcess-precision=fast
LDFLAGS = -shared -Wl,--dll,--add-stdcall-alias -Wl,-s -L.

.PHONY: all clean

all: avsreader.aui

avsreader.aui: $(OBJS)
	$(LD) $(LDFLAGS) $(XLDFLAGS) -o $@ $^

%.o: %.c .depend
	$(CC) -c $(CFLAGS) $(XCFLAGS) $< -o $@

clean:
	$(RM) *.aui *.o .depend

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS), $(CC) $(SRC) $(CFLAGS) -MT $(SRC:%.c=%.o) -MM >> .depend;)
