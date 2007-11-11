#### CONFIGURATION

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

CUSTOM_CFLAGS=-Wall -ggdb
SYS_CFLAGS=

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

# install-recursive?
install:
	$(INSTALL) ./zzgo $(DESTDIR)$(BINDIR)


clean: clean-recursive
	rm -rf zzgo *.o

-include Makefile.lib
