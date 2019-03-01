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
# See MSYS2 section for further configuration.

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
CUSTOM_CFLAGS   := -Wall -ggdb3 $(OPT) -std=gnu99 -pthread -Wsign-compare -D_GNU_SOURCE
CUSTOM_CXXFLAGS := -Wall -ggdb3 $(OPT)


###################################################################################################################
### CONFIGURATION END

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
	CUSTOM_CFLAGS += -frename-registers
endif

ifdef DATADIR
	CUSTOM_CFLAGS += -DDATA_DIR=\"$(DATADIR)\"
endif

ifdef BOARD_SIZE
	CUSTOM_CFLAGS += -DBOARD_SIZE=$(BOARD_SIZE)
endif

EXTRA_OBJS :=
EXTRA_SUBDIRS :=

##############################################################################
ifdef MSYS2
        # Try static build ?
        # MSYS2_STATIC=1

        # For dcnn build, caffe msys2 package is probably in the repos now.
        # Otherwise get one from https://github.com/lemonsqueeze/mingw-caffe
        # ('mini' / 'nohdf5' releases allow for smaller static builds)

	WIN_HAVE_NO_REGEX_SUPPORT=1

	SYS_CFLAGS  := $(TUNE)
	SYS_LDFLAGS := -pthread -L$(CAFFE_PREFIX)/bin -L$(MINGW_PREFIX)/bin
	SYS_LIBS    := -lws2_32
	CUSTOM_CXXFLAGS += -I$(MINGW_PREFIX)/include/OpenBLAS

        # Enable mingw-w64 C99 printf() / scanf() layer ?
        SYS_CFLAGS += -D__USE_MINGW_ANSI_STDIO

	ifdef WIN_HAVE_NO_REGEX_SUPPORT
		SYS_CFLAGS += -DHAVE_NO_REGEX_SUPPORT
	else
		SYS_LIBS += -lregex -ltre -lintl -liconv	# Oh, dear...
	endif

	DCNN_LIBS := -lcaffe -lboost_system-mt -lglog -lstdc++ $(SYS_LIBS)

	ifdef MSYS2_STATIC		# Static build, good luck
                # Which type of caffe package do you have ?
                # Regular caffe package is fine but pulls in hdf5 (+deps) which we don't need
                # and requires --whole-archive for static linking. This makes binaries unnecessarily
                # bloated. Choose normal, nohdf5, or mini (mini is best)
		CAFFE=normal

		ifeq ($(CAFFE), normal)
			HDF5_LIBS = -lhdf5_hl -lhdf5 -lszip -lz
		endif

		ifeq ($(CAFFE), mini)
                        # Force linking of caffe layer factory, will pull in layers we need.
			EXTRA_DCNN_OBJS := layer_factory.o
			CAFFE_STATIC_LIB = -lcaffe
		else
			CAFFE_STATIC_LIB = -Wl,--whole-archive -l:libcaffe.a -Wl,--no-whole-archive
		endif

		DCNN_LIBS := -Wl,-Bstatic $(CAFFE_STATIC_LIB)  \
			     -lboost_system-mt -lboost_thread-mt -lopenblas $(HDF5_LIBS) -lgflags_static \
			     -lglog -lprotobuf -lstdc++ -lwinpthread $(SYS_LIBS)   -Wl,-Bdynamic -lshlwapi

                # glog / gflags headers really shouldn't __declspec(dllexport) symbols for us,
                # static linking will fail with undefined __imp__xxx symbols.
                # Normally this works around it.
		SYS_CXXFLAGS += -DGOOGLE_GLOG_DLL_DECL="" -DGFLAGS_DLL_DECL=""
	endif
else
##############################################################################
ifdef MAC
	SYS_CFLAGS  := -DNO_THREAD_LOCAL
	SYS_LDFLAGS := -pthread -rdynamic
	SYS_LIBS    := -lm -ldl
	DCNN_LIBS   := -lcaffe -lboost_system -lglog -lstdc++ $(SYS_LIBS)
else
##############################################################################
# Linux
        # Static build ?
        # LINUX_STATIC=1

	SYS_CFLAGS  := $(TUNE)
	SYS_LDFLAGS := -pthread -rdynamic
	SYS_LIBS    := -lm -lrt -ldl
	DCNN_LIBS   := -lcaffe -lboost_system -lglog -lstdc++ $(SYS_LIBS)

	ifdef LINUX_STATIC
                # Which type of caffe package do you have ?
                # Regular caffe package is fine but pulls in hdf5 (+deps) which we don't need
                # and requires --whole-archive for static linking. This makes binaries unnecessarily
                # bloated. Choose normal, nohdf5, or mini (mini is best)
                # mini source: https://github.com/lemonsqueeze/caffe/tree/mini
		CAFFE=normal

		ifeq ($(CAFFE), normal)
			HDF5_LIBS = -lhdf5_serial_hl -lhdf5_serial -lsz -laec -lz
		endif

		ifeq ($(CAFFE), mini)
                        # Force linking of caffe layer factory, will pull in layers we need.
			EXTRA_DCNN_OBJS := layer_factory.o
			CAFFE_STATIC_LIB = -lcaffe
		else
			CAFFE_STATIC_LIB = -Wl,--whole-archive -l:libcaffe.a -Wl,--no-whole-archive
		endif

		SYS_LDFLAGS := -pthread -static
		DCNN_LIBS   := $(CAFFE_STATIC_LIB) -lglog -lgflags -lprotobuf -lboost_system -lboost_thread -lopenblas $(HDF5_LIBS) -lstdc++  $(SYS_LIBS)
	endif
endif
endif

ifdef CAFFE_PREFIX
	SYS_LDFLAGS += -L$(CAFFE_PREFIX)/lib -Wl,-rpath=$(CAFFE_PREFIX)/lib
	CXXFLAGS    += -I$(CAFFE_PREFIX)/include
endif

ifeq ($(DCNN), 1)
	CUSTOM_CFLAGS   += -DDCNN
	CUSTOM_CXXFLAGS += -DDCNN
	EXTRA_OBJS      += $(EXTRA_DCNN_OBJS) caffe.o dcnn.o
	SYS_LIBS := $(DCNN_LIBS)
endif

ifeq ($(FIFO), 1)
	CUSTOM_CFLAGS += -DPACHI_FIFO
	EXTRA_OBJS    += fifo.o
endif

ifeq ($(DOUBLE_FLOATING), 1)
	CUSTOM_CFLAGS += -DDOUBLE_FLOATING
endif

ifeq ($(DISTRIBUTED), 1)
	CUSTOM_CFLAGS += -DDISTRIBUTED
	EXTRA_SUBDIRS += distributed
endif

ifeq ($(PLUGINS), 1)
	CUSTOM_CFLAGS += -DPACHI_PLUGINS
endif

ifeq ($(BOARD_TESTS), 1)
	SYS_LIBS      += -lcrypto
	CUSTOM_CFLAGS += -DBOARD_TESTS
endif

ifeq ($(PROFILING), gprof)
	CUSTOM_LDFLAGS += -pg
	CUSTOM_CFLAGS  += -pg -fno-inline
else
        # Whee, an extra register!
	CUSTOM_CFLAGS += -fomit-frame-pointer
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
       board.o engine.o gogui.o gtp.o joseki.o move.o ownermap.o pachi.o pattern3.o pattern.o patternsp.o \
       patternprob.o playout.o probdist.o random.o stone.o timeinfo.o network.o fbook.o chat.o util.o

# Low-level dependencies last
SUBDIRS   = $(EXTRA_SUBDIRS) uct uct/policy t-unit t-predict engines playout tactics
DATAFILES = patterns_mm.gamma patterns_mm.spat book.dat golast19.prototxt golast.trained joseki19.gtp

###############################################################################################################
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

	rm -rf distribute 2>/dev/null;  $(INSTALL) -d distribute
	cp pachi distribute/

        ifndef MSYS2
		cd distribute  &&  strip pachi
        else
		cd distribute  &&  strip pachi.exe
		@echo "packing exe ..."
		@cd distribute  &&  upx -o p.exe pachi.exe  &&  mv p.exe pachi.exe
                ifndef MSYS2_STATIC
			@echo "copying dlls ..."
			@cd distribute; \
			    mingw=`echo $$MINGW_PREFIX | tr '/' '.' `; \
			    dlls_list="../$${mingw}_dlls"; \
			    cp `cat $$dlls_list` .
                endif
        endif

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

# 'mini' caffe static link hack.
ifdef LINUX_STATIC
layer_factory.o: $(CAFFE_PREFIX)/lib/libcaffe.a
	@echo "[AR]   $@"
	@ar x $< $@
endif

ifdef MSYS2_STATIC
layer_factory.o: $(MINGW_PREFIX)/lib/libcaffe.a
	@echo "[AR]   $@"
	@ar x $< $@
endif

-include Makefile.lib
