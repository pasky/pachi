INCLUDES=-I..

OBJS=montecarlo.o random.o replay.o

ifeq ($(DCNN), 1)
	OBJS+=dcnn.o
endif

ifeq ($(JOSEKIFIX), 1)
	OBJS+=external.o
endif

all: lib.a
lib.a: $(OBJS)

-include ../Makefile.lib
