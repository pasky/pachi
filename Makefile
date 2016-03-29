#### CONFIGURATION

# Uncomment one of the options below to change the way Pachi is built.
# Alternatively, you can pass the option to make itself, like:
# 	make MAC=1 DOUBLE=1


# Do you compile on Windows instead of Linux? Please note that the
# performance may not be optimal.
# (XXX: For now, only the mingw target is supported on Windows.
# Patches for others are welcome!)

# WIN=1

# Do you compile on MacOS/X instead of Linux? Please note that the
# performance may not be optimal.
# (XXX: We are looking for volunteers contributing support for other
# targets, like mingw/Windows.)

# MAC=1

# Compile Pachi with dcnn support ?
# You'll need to install Boost and Caffe libraries.
# If Caffe is in a custom directory you can set it here.

#DCNN=1
#CAFFE_LIB=/usr/local/lib

# By default, Pachi uses low-precision numbers within the game tree to
# conserve memory. This can become an issue with playout counts >1M,
# e.g. with extremely long thinking times or massive parallelization;
# 24 bits of floating_t mantissa become insufficient then.

# DOUBLE=1

# Enable performance profiling using gprof. Note that this also disables
# inlining, which allows more fine-grained profile, but may also distort
# it somewhat.

# PROFILING=gprof

# Enable performance profiling using google-perftools. This is much
# more accurate, fine-grained and nicer than gprof and does not change
# the way the actual binary is compiled and runs.

# PROFILING=perftools


# Target directories when running `make install`. Note that this is NOT
# quite supported yet - Pachi will work fine, but will always look for
# extra data files (such as pattern, joseki or fuseki database) only
# in the current directory, bundled database files will not be installed
# in a system directory or loaded from there.
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

# Generic compiler options. You probably do not really want to twiddle
# any of this.
# (N.B. -ffast-math breaks us; -fomit-frame-pointer is added below
# unless PROFILING=gprof.)
CUSTOM_CFLAGS?=-Wall -ggdb3 -O3 -std=gnu99 -frename-registers -pthread -Wsign-compare -D_GNU_SOURCE
CUSTOM_CXXFLAGS?=-Wall -ggdb3 -O3

### CONFIGURATION END


ifdef WIN
	SYS_CFLAGS?=
	SYS_LDFLAGS?=-pthread
	SYS_LIBS?=-lm -lws2_32

ifdef WIN_HAVE_NO_REGEX_SUPPORT
	SYS_CFLAGS += -DHAVE_NO_REGEX_SUPPORT
else
	SYS_LIBS += -lregex
endif

else
ifdef MAC
	SYS_CFLAGS?=-DNO_THREAD_LOCAL
	SYS_LDFLAGS?=-pthread -rdynamic
	SYS_LIBS?=-lm -ldl
else
	SYS_CFLAGS?=-march=native
	SYS_LDFLAGS?=-pthread -rdynamic
	SYS_LIBS?=-lm -lrt -ldl
endif
endif

ifdef CAFFE_LIB
	SYS_LDFLAGS+=-L$(CAFFE_LIB)
endif

ifdef DCNN
	CUSTOM_CFLAGS+=-DDCNN
	CUSTOM_CXXFLAGS+=-DDCNN
	SYS_LIBS:=-lcaffe -lboost_system -lstdc++ $(SYS_LIBS)
endif

ifdef DOUBLE
	CUSTOM_CFLAGS+=-DDOUBLE
endif

ifeq ($(PROFILING), gprof)
	CUSTOM_LDFLAGS+=-pg
	CUSTOM_CFLAGS+=-pg -fno-inline
else
	# Whee, an extra register!
	CUSTOM_CFLAGS+=-fomit-frame-pointer
ifeq ($(PROFILING), perftools)
	SYS_LIBS+=-lprofiler
endif
endif

ifndef LD
LD=ld
endif

ifndef AR
AR=ar
endif

ifndef INSTALL
INSTALL=/usr/bin/install
endif

export
unexport INCLUDES
INCLUDES=-I.


OBJS=board.o gtp.o move.o ownermap.o pattern3.o pattern.o patternsp.o patternprob.o playout.o probdist.o random.o stone.o timeinfo.o network.o fbook.o chat.o
ifdef DCNN
	OBJS+=dcnn.o
endif
SUBDIRS=random replay patternscan patternplay joseki montecarlo uct uct/policy playout tactics t-unit distributed

all: all-recursive pachi

LOCALLIBS=random/random.a replay/replay.a patternscan/patternscan.a patternplay/patternplay.a joseki/joseki.a montecarlo/montecarlo.a uct/uct.a uct/policy/uctpolicy.a playout/playout.a tactics/tactics.a t-unit/test.a distributed/distributed.a
$(LOCALLIBS): all-recursive
	@
pachi: $(OBJS) pachi.o $(LOCALLIBS)
	$(call cmd,link)

# Use runtime gcc profiling for extra optimization. This used to be a large
# bonus but nowadays, it's rarely worth the trouble.
.PHONY: pachi-profiled
pachi-profiled:
	@make clean all XLDFLAGS=-fprofile-generate XCFLAGS="-fprofile-generate -fomit-frame-pointer -frename-registers"
	./pachi -t =5000 no_tbook <tools/genmove19.gtp
	@make clean all clean-profiled XLDFLAGS=-fprofile-use XCFLAGS="-fprofile-use -fomit-frame-pointer -frename-registers"

# install-recursive?
install:
	$(INSTALL) ./pachi $(DESTDIR)$(BINDIR)


clean: clean-recursive
	rm -f pachi *.o

clean-profiled: clean-profiled-recursive
	rm -f *.gcda *.gcno

-include Makefile.lib
