INCLUDES  = -I../..
CFLAGS   += -fPIC
LDFLAGS   = -shared
LIBS      =

OBJS    = example.o wolf.o

PLUGINS = $(OBJS:%.o=%.so)

all: $(PLUGINS)

example.so: example.o
	$(call cmd,link)

wolf.so: wolf.o
	$(call cmd,link)

clean::
	-@rm $(PLUGINS)  2>/dev/null

-include ../../Makefile.lib
