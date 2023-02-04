INCLUDES=-I..

OBJS=external.o montecarlo.o random.o replay.o

ifeq ($(DCNN), 1)
	OBJS+=dcnn.o
endif

all: lib.a
lib.a: $(OBJS)

-include ../Makefile.lib
