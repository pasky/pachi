INCLUDES=-I..
SUBDIRS=

OBJS := dynkomi.o tree.o uct.o prior.o search.o walk.o

ifeq ($(PLUGINS), 1)
	SUBDIRS += plugins
	OBJS += plugins.o
endif

ifeq ($(DISTRIBUTED), 1)
	OBJS += slave.o
endif

all: lib.a
	+@make all-recursive

lib.a: $(OBJS)

clean:: clean-recursive


-include ../Makefile.lib
