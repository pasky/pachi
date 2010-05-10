INCLUDES=-I..
OBJS=distributed.o protocol.o merge.o

all: distributed.a
distributed.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
