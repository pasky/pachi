INCLUDES=-I..
OBJS=test.o

all: test.a
test.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
