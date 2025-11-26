############################ CONFIGURATION ############################

# Tweak options below to change the way Pachi is built.
# Alternatively, you can pass the option to make itself, like:
# 	make MAC=1 DOUBLE_FLOATING=1
# or use the short aliases (make fast, make generic ...)


################################ Build ################################

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


############################ Deep Learning ############################

# Compile Pachi with dcnn support ?
# You'll need to install Boost and Caffe libraries.
# If Caffe is in a custom directory you can set it here.

DCNN=1
# CAFFE_PREFIX=/usr/local/caffe

# Supported networks:
# Comment out those you don't need for speed.

DCNN_DETLEF=1
DCNN_DARKFOREST=1


############################ Build Options ###########################

# Fixed board size. Set this to enable more aggressive optimizations
# if you only play on 19x19. Pachi won't be able to play on other
# board sizes.

# BOARD_SIZE=19

# Build josekifix module ?	 (DCNN must be enabled as well)
# Provides fixes for joseki lines that dcnn plays poorly, more varied
# fusekis as black and let Pachi play modern josekis with --modern-joseki
# option. Uses Katago as joseki engine (cpu version is built by default,
# or you can use another version, see below).

JOSEKIFIX=1

# Running multiple Pachi instances ? Enable this to coordinate them so that
# only one takes the cpu at a time. If your system uses systemd beware !
# Go and read note at top of fifo.c

# FIFO=1


############################### Katago ################################

# JOSEKIFIX module uses katago as joseki engine.
# If enabled cpu version of katago will be built. To use already
# installed version instead disable BUILD_KATAGO and set KATAGO_BINARY
# to katago (or use --external-joseki-engine pachi option).

ifeq ($(JOSEKIFIX), 1)
	BUILD_KATAGO=1
endif

KATAGO_BINARY=katago_cpu

# Uncomment to use AVX2 instructions if your cpu supports them.
# Will speed up Eigen, but won't work on old cpus.

# KATAGO_AVX=1

# Build uses same optimization / tuning options set in this Makefile.
# To rebuild after changing OPT / GENERIC options do:
#     $ make clean ; make -j4

# Default config and model to use:
KATAGO_CONFIG=katago.cfg
# 10b model for raspberry pi or slow pc without AVX support,
KATAGO_MODEL=g170e-b10c128-s1141046784-d204142634.bin.gz
# 15b model for fast pc with AVX support.
# KATAGO_MODEL=g170e-b15c192-s1672170752-d466197061.bin.gz

# Config and model paths
KATAGO_MODEL_PATH=josekifix/katago/$(KATAGO_MODEL)
KATAGO_CONFIG_PATH=josekifix/katago/$(KATAGO_CONFIG)


############################### Install ###############################

# Target directories when running 'make install'.
# Pachi will look for its data files (dcnn, pattern, joseki etc) in
# system directory below (in addition to current directory / DATA_DIR
# environment variable if present).
PREFIX=$(DESTDIR)/usr
BINDIR=$(PREFIX)/bin
DATADIR=$(PREFIX)/share/pachi-go

# Generic compiler options. You probably do not really want to twiddle
# any of this.
# (N.B. -ffast-math breaks us; -fomit-frame-pointer is added below
# unless PROFILING=gprof.)
OPT ?= -O3
COMMON_FLAGS := -Wall -ggdb3 $(OPT) -D_GNU_SOURCE
CFLAGS       := -std=gnu99 -pthread -Wsign-compare -Wno-format-zero-length -Winit-self -Wimplicit-fallthrough
CXXFLAGS     := -std=c++11


############################ Special Builds ############################

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


############################# Development #############################

# Compile Pachi with extra sanity checks ?
# Good when debugging / testing code. Pachi will run slightly slower
# but low-level code benefits from extra checking. Helps find errors
# in the engine that might otherwise go unnoticed. Make sure to run
# tests and play games with EXTRA_CHECKS on before a new release. If
# something breaks with EXTRA_CHECKS on it's a bug.

# EXTRA_CHECKS=1

# Enable Address Sanitizer build. Pachi will run much slower but every
# memory access is checked (think Valgrind on steroids). Both gcc and
# clang support Address Sanitizer on a number of architectures (linux
# x86/amd64 recommended).

# ASAN=1

# Build extra engines used for development / debugging ?
# You need this to retrain mm patterns.

# EXTRA_ENGINES=1

# Build extra tests ?
# Enable this to test board implementation.

# BOARD_TESTS=1


############################## Profiling ##############################

# Enable performance profiling using gprof. Note that this also disables
# inlining, which allows more fine-grained profile, but may also distort
# it somewhat.

# PROFILING=gprof

# Enable performance profiling using google-perftools. This is much
# more accurate, fine-grained and nicer than gprof and does not change
# the way the actual binary is compiled and runs.

# PROFILING=perftools


#########################################################################
### CONFIGURATION END

# Main rules + aliases
# Aliases are nice, but don't ask too much: 'make quick 19' won't do what
# you expect for example (use 'make OPT=-O0 BOARD_SIZE=19' instead)

all: build.h
	+@make all-recursive pachi
        ifeq ($(BUILD_KATAGO), 1)
	    +@make katago_cpu
        endif

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


#######################################################################
# Sanity checks

ifeq ($(JOSEKIFIX), 1)
ifndef DCNN
$(error DCNN must be enabled for JOSEKIFIX build)
endif
endif


#######################################################################
# Variables

MAKEFLAGS += --no-print-directory
ARCH = $(shell uname -m)

ifeq ($(GENERIC), 1)
	TUNE := -mtune=generic
else
	TUNE := -march=native
endif

ifndef NO_FRENAME_REGISTERS
	COMMON_FLAGS += -frename-registers
endif

ifdef DATADIR
	COMMON_FLAGS += -DDATA_DIR='"$(DATADIR)"'
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
	EXTRA_SUBDIRS  += dcnn
	EXTRA_OBJS     += $(EXTRA_DCNN_OBJS)
	LIBS           := $(DCNN_LIBS)
else
	DCNN_DETLEF = 0
	DCNN_DARKFOREST = 0
endif

ifeq ($(DCNN_DETLEF), 1)
	COMMON_FLAGS += -DDCNN_DETLEF
endif

ifeq ($(DCNN_DARKFOREST), 1)
	COMMON_FLAGS += -DDCNN_DARKFOREST
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

ifeq ($(EXTRA_ENGINES), 1)
	COMMON_FLAGS += -DEXTRA_ENGINES
endif

ifeq ($(JOSEKIFIX), 1)
	COMMON_FLAGS    += -DJOSEKIFIX -DKATAGO_BINARY='"$(KATAGO_BINARY)"' -DKATAGO_CONFIG='"$(KATAGO_CONFIG)"' -DKATAGO_MODEL='"$(KATAGO_MODEL)"'
	EXTRA_DATAFILES += josekifix.gtp $(KATAGO_CONFIG_PATH) $(KATAGO_MODEL_PATH)
endif

ifeq ($(BOARD_TESTS), 1)
	LIBS          += -lcrypto
	COMMON_FLAGS  += -DBOARD_TESTS
endif

ifeq ($(EXTRA_CHECKS), 1)
	COMMON_FLAGS  += -DEXTRA_CHECKS
endif

ifeq ($(PROFILING), gprof)
	LDFLAGS      += -pg
	COMMON_FLAGS += -pg -fno-inline
endif

ifeq ($(PROFILING), perftools)
	LIBS         += -lprofiler
endif

ifeq ($(ASAN), 1)
	COMMON_FLAGS += -fsanitize=address -fno-omit-frame-pointer
	LIBS         := -lasan $(LIBS)
else
        # Whee, an extra register!
	COMMON_FLAGS += -fomit-frame-pointer
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

CFLAGS   := $(COMMON_FLAGS) $(CFLAGS)   $(XCFLAGS)
CXXFLAGS := $(COMMON_FLAGS) $(CXXFLAGS) $(XCFLAGS)
LDFLAGS  := $(LDFLAGS) $(XLDFLAGS)

export
unexport INCLUDES
INCLUDES=-I.

OBJS = $(EXTRA_OBJS) \
       board.o board_undo.o engine.o gogui.o gtp.o move.o ownermap.o pachi.o pattern3.o \
       playout.o random.o stone.o timeinfo.o fbook.o chat.o util.o

# Low-level dependencies last
SUBDIRS   = $(EXTRA_SUBDIRS) engines joseki josekifix pattern playout tactics t-predict t-unit uct uct/policy
DATAFILES = $(EXTRA_DATAFILES) detlef54.prototxt detlef54.trained joseki19.gtp opening.dat patterns_mm.gamma patterns_mm.spat 


############################################################################################################

LOCALLIBS=$(SUBDIRS:%=%/lib.a)
$(LOCALLIBS): all-recursive
	@

pachi: $(OBJS) $(LOCALLIBS) $(EXTRA_DEPS)
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
build.h: build.h.git Makefile
	+@make spudfrog
	@echo "[make] build.h"
	@cp build.h.git $@
	@CC="$(CC)" CFLAGS="$(CFLAGS)" ./genbuild >> $@

build.h.git: .git/HEAD .git/index
	@./genbuild --git > $@

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

test_external_engine: FORCE
	+@make -C t-unit test_external_engine

# Regression tests
regtest: FORCE
	+@make -C t-regress regtest


############################## Katago ##############################

KATAGO_CMAKE_OPTS := -DUSE_BACKEND=EIGEN -DCMAKE_CXX_FLAGS='$(OPT) $(TUNE)'
MAKEJOBS := $(filter -j%,$(MAKEFLAGS))

ifeq ($(KATAGO_AVX), 1)
	KATAGO_CMAKE_OPTS += -DUSE_AVX2=1
endif

katago_banner:
	@./spudfrog katago

# Get source
katago:
	@echo "[GIT]"
	@git clone --depth 1 -b v1.15.3 -c advice.detachedHead=0 "https://github.com/lightvector/KataGo" katago
	@echo ""
	@echo "[PATCH]"
	@patch -p1 < josekifix/katago/katago_build.patch
	@echo ""

# Run cmake and find build type (make or ninja)
katago/cpp/build:
	@echo "[CMAKE]"
	@echo "cmake $(KATAGO_CMAKE_OPTS) ."
	@cd katago/cpp  && cmake $(KATAGO_CMAKE_OPTS) .
	@[ -f katago/cpp/Makefile ]    && echo "make"  > $@; \
	 [ -f katago/cpp/build.ninja ] && echo "ninja" > $@; \
	 [ -f $@ ] || ( echo "Looks like cmake wants to use something other than make or ninja, i don't handle that." ; exit 1 )
	@echo ""

.NOTPARALLEL: katago_build
katago_build: katago_banner katago katago/cpp/build

# Configure and build
katago/cpp/katago_cpu:
	+@make katago_build
	+@cd katago/cpp && \
          if grep -q ninja build; then \
	    echo "[NINJA]"; ninja; \
	  else \
	    echo "[MAKE]"; make $(MAKEJOBS); \
	  fi
	@mv  katago/cpp/katago katago/cpp/katago_cpu

katago_cpu: katago/cpp/katago_cpu

katago_clean: FORCE
	-@ [ -f katago/cpp/Makefile ] && cd katago/cpp && make clean
	-@ [ -f katago/cpp/build.ninja ] && cd katago/cpp && ninja clean
	-@cd katago/cpp && rm -rf build Makefile build.ninja CMakeFiles CMakeCache.txt  2>/dev/null


############################## Install ##############################

# Prepare for install
distribute: FORCE
        ifneq ($(GENERIC), 1)
		@echo "WARNING: Don't distribute binaries built with -march=native !"
        endif

	@rm -rf distribute 2>/dev/null;  $(INSTALL) -d distribute
	cp pachi distribute/
        ifeq ($(BUILD_KATAGO), 1)
	    cp katago/cpp/katago_cpu distribute/
        endif
	+@make strip      # arch specific stuff

# Install everything
install: install-bin install-data

install-bin: pachi katago_cpu
	+@make distribute
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) distribute/pachi $(BINDIR)/
        ifeq ($(BUILD_KATAGO), 1)
	    $(INSTALL) distribute/katago_cpu $(BINDIR)/
        endif

install-data: $(DATAFILES)
	$(INSTALL) -d $(DATADIR)
	@for file in $(DATAFILES); do                             \
		if [ -f $$file ]; then                            \
			echo $(INSTALL) $$file $(DATADIR)/;       \
			$(INSTALL) $$file $(DATADIR)/;            \
		else                                              \
			echo "FATAL: datafile '$$file' is missing";   \
			exit 1;                                   \
		fi                                                \
	done;

# Get missing datafiles
datafiles: $(DATAFILES)

# Download dcnn files from github
detlef54.prototxt detlef54.trained:
	@echo "Get dcnn datafiles:" ; echo ""
	wget -c -O detlef54.zip 'https://github.com/pasky/pachi/releases/download/pachi_networks/detlef54.zip'
	@unzip -q -o detlef54.zip  detlef54.prototxt detlef54.trained
	@rm detlef54.zip

# Download katago 10b model
josekifix/katago/g170e-b10c128-s1141046784-d204142634.bin.gz:
	@echo "Get katago model:  (g170e-b10c128)" ; echo ""
	wget -c -O model.bin.gz 'https://github.com/pasky/pachi/releases/download/katago_models/g170e-b10c128-s1141046784-d204142634.bin.gz'
	@mv model.bin.gz $@

# Download katago 15b model
josekifix/katago/g170e-b15c192-s1672170752-d466197061.bin.gz:
	@echo "Get katago model:  (g170e-b15c192)" ; echo ""
	wget -c -O model.bin.gz 'https://github.com/pasky/pachi/releases/download/katago_models/g170e-b15c192-s1672170752-d466197061.bin.gz'
	@mv model.bin.gz $@

# Generic clean rule is in Makefile.lib
clean:: clean-recursive
	-@ [ -d katago ] && make katago_clean
	-@rm pachi build.h* >/dev/null 2>&1
	@echo ""

clean-profiled:: clean-profiled-recursive

TAGS: FORCE
	@echo "Generating TAGS ..."
	@etags `find . -name "*.[ch]" -o -name "*.cpp"`

FORCE:


-include Makefile.lib
