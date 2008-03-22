INCLUDES=-I..
OBJS=moggy.o old.o

all: playout.a
playout.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
