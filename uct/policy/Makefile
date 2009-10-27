INCLUDES=-I../..
OBJS=generic.o ucb1.o ucb1amaf.o

all: uctpolicy.a
uctpolicy.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../../Makefile.lib
