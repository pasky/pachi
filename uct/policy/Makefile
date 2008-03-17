INCLUDES=-I../..
OBJS=ucb1.o

all: uctpolicy.a
uctpolicy.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../../Makefile.lib
