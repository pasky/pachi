INCLUDES=-I..
OBJS=replay.o

all: replay.a
replay.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
