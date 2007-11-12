INCLUDES=-I..
OBJS=random.o

all: random.a
random.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
