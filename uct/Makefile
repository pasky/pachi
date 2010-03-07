INCLUDES=-I..
OBJS=dynkomi.o tree.o uct.o prior.o walk.o

all: uct.a
uct.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
