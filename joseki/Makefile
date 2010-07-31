INCLUDES=-I..
OBJS=joseki.o base.o

all: joseki.a
joseki.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
