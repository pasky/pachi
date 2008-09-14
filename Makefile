#### CONFIGURATION

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

# -ffast-math breaks us
CUSTOM_CFLAGS=-Wall -ggdb3 -O3 -march=i686 -std=gnu99 -fomit-frame-pointer -frename-registers
SYS_CFLAGS=
LDFLAGS=-lm

# Profiling:
# LDFLAGS+=-pg
# CUSTOM_CFLAGS+= -pg -fno-inline

LD=ld
AR=ar

### CONFIGURATION END

ifndef INSTALL
INSTALL=/usr/bin/install
endif

export
unexport INCLUDES
INCLUDES=-I.


OBJS=board.o gtp.o move.o playout.o random.o stone.o zzgo.o
SUBDIRS=random montecarlo uct uct/policy playout

all: all-recursive zzgo

LOCALLIBS=random/random.a montecarlo/montecarlo.a uct/uct.a uct/policy/uctpolicy.a playout/playout.a
zzgo: $(OBJS) $(LOCALLIBS)
	$(call cmd,link)

.PHONY: zzgo-profiled
zzgo-profiled:
	@make clean all XLDFLAGS=-fprofile-generate XCFLAGS="-fprofile-generate -fomit-frame-pointer -frename-registers"
	echo -e 'boardsize 9\nkomi 0\nclear_board\ngenmove black\ngenmove white' | ./zzgo games=5000
	@make clean all clean-profiled XLDFLAGS=-fprofile-use XCFLAGS="-fprofile-use -fomit-frame-pointer -frename-registers"

# install-recursive?
install:
	$(INSTALL) ./zzgo $(DESTDIR)$(BINDIR)


clean: clean-recursive
	rm -f zzgo *.o

clean-profiled: clean-profiled-recursive
	rm -f *.gcda *.gcno

-include Makefile.lib
