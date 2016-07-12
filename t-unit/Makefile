INCLUDES=-I..
OBJS=test.o test_undo.o

all: test.a
test.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
