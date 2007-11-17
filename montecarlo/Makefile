INCLUDES=-I..
OBJS=hint.o montecarlo.o

all: montecarlo.a
montecarlo.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
