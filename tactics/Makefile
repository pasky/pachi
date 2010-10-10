INCLUDES=-I..
OBJS=ladder.o selfatari.o util.o

all: tactics.a
tactics.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
