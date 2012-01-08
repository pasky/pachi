INCLUDES=-I..
OBJS=patternplay.o

all: patternplay.a
patternplay.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
