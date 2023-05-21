INCLUDES=-I..

OBJS := pattern.o pattern_engine.o prob.o spatial.o

ifeq ($(EXTRA_ENGINES), 1)
	OBJS += patternscan.o
endif

all: lib.a
lib.a: $(OBJS)

-include ../Makefile.lib
