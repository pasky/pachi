INCLUDES=-I..
OBJS=1lib.o 2lib.o nlib.o ladder.o nakade.o selfatari.o util.o

all: tactics.a
tactics.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
