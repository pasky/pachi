INCLUDES=-I..

OBJS=caffe.o dcnn.o blunder.o dcnn_engine.o

ifeq ($(EXTRA_ENGINES), 1)
	OBJS += blunderscan.o
endif

all: lib.a
lib.a: $(OBJS)

-include ../Makefile.lib
