#### CONFIGURATION

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

CUSTOM_CFLAGS=-Wall -ggdb -O3
SYS_CFLAGS=

# Profiling:
# LD_FLAGS=-pg
# CUSTOM_CFLAGS+= -pg

LD=ld
AR=ar

### CONFIGURATION END

ifndef INSTALL
INSTALL=/usr/bin/install
endif

export
unexport INCLUDES
INCLUDES=-I.


OBJS=board.o gtp.o move.o stone.o zzgo.o
SUBDIRS=random montecarlo

all: all-recursive zzgo

LOCALLIBS=random/random.a montecarlo/montecarlo.a
zzgo: $(OBJS) $(LOCALLIBS)
	$(call cmd,link)

.PHONY: zzgo-profiled
zzgo-profiled:
	@make all LDFLAGS=-fprofile-generate XCFLAGS=-fprofile-generate
	echo -e 'boardsize 9\nkomi 0\nclear_board\ngenmove black\ngenmove white' | ./zzgo games=200
	@make clean all clean-profiled LDFLAGS=-fprofile-use XCFLAGS=-fprofile-use

# install-recursive?
install:
	$(INSTALL) ./zzgo $(DESTDIR)$(BINDIR)


clean: clean-recursive
	rm -f zzgo *.o

clean-profiled: clean-profiled-recursive
	rm -f *.gcda *.gcno

-include Makefile.lib
