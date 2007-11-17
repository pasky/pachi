INCLUDES=-I..
OBJS=montecasino.o

all: montecasino.a
montecasino.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
