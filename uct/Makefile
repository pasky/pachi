INCLUDES=-I..

OBJS := dynkomi.o tree.o uct.o prior.o search.o walk.o

ifeq ($(PLUGINS), 1)
	OBJS += plugins.o
endif

ifeq ($(DISTRIBUTED), 1)
	OBJS += slave.o
endif

all: lib.a
lib.a: $(OBJS)


-include ../Makefile.lib
