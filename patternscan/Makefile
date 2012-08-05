INCLUDES=-I..
OBJS=patternscan.o

all: patternscan.a
patternscan.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
