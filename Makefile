#### CONFIGURATION

# PROFILING=1

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

# -ffast-math breaks us
CUSTOM_CFLAGS=-Wall -ggdb3 -O3 -std=gnu99 -frename-registers -pthread -Wsign-compare -D_GNU_SOURCE
ifdef MAC
	SYS_CFLAGS=-DNO_THREAD_LOCAL
	LDFLAGS=-lm -pthread -ldl -rdynamic
else
	SYS_CFLAGS=-march=native
	LDFLAGS=-lm -pthread -lrt -ldl -rdynamic
endif

# Use make DOUBLE=1 in large configurations with counts > 1M
# where 24 bits of floating_t mantissa become insufficient.
ifdef DOUBLE
	CUSTOM_CFLAGS+=-DDOUBLE
endif

# Profiling:
ifdef PROFILING
	LDFLAGS+=-pg
	CUSTOM_CFLAGS+=-pg -fno-inline
else
	# Whee, an extra register!
	CUSTOM_CFLAGS+=-fomit-frame-pointer
endif

LD=ld
AR=ar

### CONFIGURATION END

ifndef INSTALL
INSTALL=/usr/bin/install
endif

export
unexport INCLUDES
INCLUDES=-I.


OBJS=board.o gtp.o move.o ownermap.o pattern3.o playout.o probdist.o random.o stone.o timeinfo.o network.o fbook.o
SUBDIRS=random replay joseki montecarlo uct uct/policy playout tactics t-unit distributed

all: all-recursive zzgo

LOCALLIBS=random/random.a replay/replay.a joseki/joseki.a montecarlo/montecarlo.a uct/uct.a uct/policy/uctpolicy.a playout/playout.a tactics/tactics.a t-unit/test.a distributed/distributed.a
zzgo: $(OBJS) zzgo.o $(LOCALLIBS)
	$(call cmd,link)

.PHONY: zzgo-profiled
zzgo-profiled:
	@make clean all XLDFLAGS=-fprofile-generate XCFLAGS="-fprofile-generate -fomit-frame-pointer -frename-registers"
	./zzgo -t =5000 no_tbook <tools/genmove19.gtp
	@make clean all clean-profiled XLDFLAGS=-fprofile-use XCFLAGS="-fprofile-use -fomit-frame-pointer -frename-registers"

# install-recursive?
install:
	$(INSTALL) ./zzgo $(DESTDIR)$(BINDIR)


clean: clean-recursive
	rm -f zzgo *.o

clean-profiled: clean-profiled-recursive
	rm -f *.gcda *.gcno

-include Makefile.lib
