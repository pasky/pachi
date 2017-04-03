INCLUDES=-I..
OBJS=predict.o

all: predict.a
predict.a: $(OBJS)

clean:
	rm -f *.o *.a
clean-profiled:
	rm -f *.gcda *.gcno

-include ../Makefile.lib
