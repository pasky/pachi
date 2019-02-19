#### CONFIGURATION

# Uncomment one of the options below to change the way Pachi is built.
# Alternatively, you can pass the option to make itself, like:
# 	make MAC=1 DOUBLE_FLOATING=1
# or use the short aliases (make quick, make generic ...)

# Generic build ?
# If binary will be distributed you need this !
# Otherwise you may do without to enable more aggressive optimizations
# for this machine only.

# GENERIC=1

# Do you compile on Windows instead of Linux ?
# Please note that performance may not be optimal.
# To compile in msys2 with mingw-w64, uncomment the following line.
# See Makefile.msys2 for further configuration.

# MSYS2=1

# Do you compile on MacOS/X instead of Linux?
# Please note that performance may not be optimal.

# MAC=1

# Compile Pachi with dcnn support ?
# You'll need to install Boost and Caffe libraries.
# If Caffe is in a custom directory you can set it here.

DCNN=1
# CAFFE_PREFIX=/usr/local/caffe

# Fixed board size. Set this to enable more aggressive optimizations
# if you only play on 19x19. Pachi won't be able to play on other
# board sizes.

# BOARD_SIZE=19

# Running multiple Pachi instances ? Enable this to coordinate them so that
# only one takes the cpu at a time. If your system uses systemd beware !
# Go and read note at top of fifo.c

# FIFO=1

# By default, Pachi uses low-precision numbers within the game tree to
# conserve memory. This can become an issue with playout counts >1M,
# e.g. with extremely long thinking times or massive parallelization;
# 24 bits of floating_t mantissa become insufficient then.

# DOUBLE_FLOATING=1

# Enable distributed engine for cluster play ?

# DISTRIBUTED=1
# NETWORK=1

# Compile Pachi with external plugin support ?
# If unsure leave disabled, you most likely don't need it.

# PLUGINS=1

# Compile extra tests ? Enable this to test board implementation.
# BOARD_TESTS=1

# Enable performance profiling using gprof. Note that this also disables
# inlining, which allows more fine-grained profile, but may also distort
# it somewhat.

# PROFILING=gprof

# Enable performance profiling using google-perftools. This is much
# more accurate, fine-grained and nicer than gprof and does not change
# the way the actual binary is compiled and runs.

# PROFILING=perftools


# Target directories when running 'make install' / 'make install-data'.
# Pachi will look for extra data files (such as dcnn, pattern, joseki or
# fuseki database) in system directory below in addition to current directory
# (or DATA_DIR environment variable if present).
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/pachi

# Generic compiler options. You probably do not really want to twiddle
# any of this.
# (N.B. -ffast-math breaks us; -fomit-frame-pointer is added below
# unless PROFILING=gprof.)
OPT ?= -O3
COMMON_FLAGS := -Wall -ggdb3 $(OPT) -D_GNU_SOURCE
CFLAGS       := -std=gnu99 -pthread -Wsign-compare -Wno-format-zero-length
CXXFLAGS     := -std=c++11


###################################################################################################################
### CONFIGURATION END

# Main rule + aliases
# Aliases are nice, but don't ask too much: 'make quick 19' won't do what
# you expect for example (use 'make OPT=-O0 BOARD_SIZE=19' instead)

all: build.h
	+@make all-recursive pachi

debug fast quick O0:
	+@make OPT=-O0

opt slow O3:
	+@make OPT=-O3

generic:
	+@make GENERIC=1

native:
	+@make GENERIC=0

nodcnn:
	+@make DCNN=0

19:
	+@make BOARD_SIZE=19

double:
	+@make DOUBLE_FLOATING=1


###############################################################################################################

MAKEFLAGS += --no-print-directory
ARCH = $(shell uname -m)
TUNE := -march=native

ifeq ($(findstring armv7, $(ARCH)), armv7)
        # -march=native targets armv6 by default on Raspbian ...
	TUNE := -march=armv7-a
endif

ifeq ($(GENERIC), 1)
	TUNE := -mtune=generic
endif

ifndef NO_FRENAME_REGISTERS
	COMMON_FLAGS += -frename-registers
endif

ifdef DATADIR
	COMMON_FLAGS += -DDATA_DIR=\"$(DATADIR)\"
endif

ifdef BOARD_SIZE
	COMMON_FLAGS += -DBOARD_SIZE=$(BOARD_SIZE)
endif

EXTRA_OBJS :=
EXTRA_SUBDIRS :=

ifdef MSYS2
	-include Makefile.msys2
else
ifdef MAC
	-include Makefile.mac
else
	-include Makefile.linux
endif
endif

ifdef CAFFE_PREFIX
	LDFLAGS  += -L$(CAFFE_PREFIX)/lib -Wl,-rpath=$(CAFFE_PREFIX)/lib
	CXXFLAGS += -I$(CAFFE_PREFIX)/include
endif

ifeq ($(DCNN), 1)
	COMMON_FLAGS   += -DDCNN
	EXTRA_OBJS     += $(EXTRA_DCNN_OBJS) caffe.o dcnn.o
	SYS_LIBS := $(DCNN_LIBS)
endif

ifeq ($(FIFO), 1)
	COMMON_FLAGS += -DPACHI_FIFO
	EXTRA_OBJS   += fifo.o
endif

ifeq ($(NETWORK), 1)
	COMMON_FLAGS += -DNETWORK
	EXTRA_OBJS   += network.o
endif

ifeq ($(DOUBLE_FLOATING), 1)
	COMMON_FLAGS += -DDOUBLE_FLOATING
endif

ifeq ($(DISTRIBUTED), 1)
	COMMON_FLAGS  += -DDISTRIBUTED
	EXTRA_SUBDIRS += distributed
endif

ifeq ($(PLUGINS), 1)
	COMMON_FLAGS += -DPACHI_PLUGINS
endif

ifeq ($(BOARD_TESTS), 1)
	SYS_LIBS      += -lcrypto
	COMMON_FLAGS  += -DBOARD_TESTS
endif

ifeq ($(PROFILING), gprof)
	LDFLAGS      += -pg
	COMMON_FLAGS += -pg -fno-inline
else
        # Whee, an extra register!
	COMMON_FLAGS += -fomit-frame-pointer
ifeq ($(PROFILING), perftools)
	SYS_LIBS += -lprofiler
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

OBJS = $(EXTRA_OBJS) \
       board.o board_undo.o engine.o gogui.o gtp.o joseki.o move.o ownermap.o pachi.o pattern3.o pattern.o \
       patternsp.o patternprob.o playout.o random.o stone.o timeinfo.o fbook.o chat.o util.o

# Low-level dependencies last
SUBDIRS   = $(EXTRA_SUBDIRS) uct uct/policy t-unit t-predict engines playout tactics
DATAFILES = patterns_mm.gamma patterns_mm.spat book.dat golast19.prototxt golast.trained joseki19.gtp


###############################################################################################################

LOCALLIBS=$(SUBDIRS:%=%/lib.a)
$(LOCALLIBS): all-recursive
	@

pachi: $(OBJS) $(LOCALLIBS)
	$(call cmd,link)

# Use runtime gcc profiling for extra optimization. This used to be a large
# bonus but nowadays, it's rarely worth the trouble.
.PHONY: pachi-profiled
pachi-profiled:
	@make clean all XLDFLAGS=-fprofile-generate XCFLAGS="-fprofile-generate -fomit-frame-pointer -frename-registers"
	./pachi -t =5000 no_tbook < gtp/genmove_both.gtp
	@make clean all clean-profiled XLDFLAGS=-fprofile-use XCFLAGS="-fprofile-use -fomit-frame-pointer -frename-registers"

# Pachi build attendant
.PHONY: spudfrog
spudfrog: FORCE
	@CC="$(CC)" CFLAGS="$(CFLAGS)" ./spudfrog

# Build info
build.h: .git/HEAD .git/index Makefile
	+@make spudfrog
	@echo "[make] build.h"
	@CC="$(CC)" CFLAGS="$(CFLAGS)" ./genbuild > $@

# Unit tests
test: FORCE
	+@make -C t-unit test

test_gtp: FORCE
	+@make -C t-unit test_gtp

test_board: FORCE
	+@make -C t-unit test_board

test_moggy: FORCE
	+@make -C t-unit test_moggy

test_spatial: FORCE
	+@make -C t-unit test_spatial


# Prepare for install
distribute: FORCE
        ifneq ($(GENERIC), 1)
		@echo "WARNING: Don't distribute binaries built with -march=native !"
        endif

	@rm -rf distribute 2>/dev/null;  $(INSTALL) -d distribute
	cp pachi distribute/
	+@make strip      # arch specific stuff

# install-recursive?
install: distribute
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) distribute/pachi $(BINDIR)/

install-data:
	$(INSTALL) -d $(DATADIR)
	@for file in $(DATAFILES); do                               \
		if [ -f $$file ]; then                              \
                        echo $(INSTALL) $$file $(DATADIR)/;         \
			$(INSTALL) $$file $(DATADIR)/;              \
		else                                                \
			echo "WARNING: $$file datafile is missing"; \
                fi                                                  \
	done;

# Generic clean rule is in Makefile.lib
clean:: clean-recursive
	-@rm pachi build.h >/dev/null 2>&1
	@echo ""

clean-profiled:: clean-profiled-recursive

TAGS: FORCE
	@echo "Generating TAGS ..."
	@etags `find . -name "*.[ch]" -o -name "*.cpp"`

FORCE:


-include Makefile.lib
