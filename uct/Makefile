INCLUDES=-I..

OBJS := dynkomi.o tree.o uct.o prior.o search.o slave.o walk.o

ifeq ($(PLUGINS), 1)
	OBJS += plugins.o
endif

all: lib.a
lib.a: $(OBJS)


-include ../Makefile.lib
