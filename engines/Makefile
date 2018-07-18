INCLUDES=-I..

OBJS=montecarlo.o patternplay.o patternscan.o random.o replay.o

ifeq ($(DCNN), 1)
	OBJS+=dcnn.o
endif

ifeq ($(JOSEKI), 1)
	OBJS+=joseki.o josekibase.o
endif

all: lib.a
lib.a: $(OBJS)


-include ../Makefile.lib
