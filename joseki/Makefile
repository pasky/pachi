INCLUDES=-I..

OBJS := joseki.o joseki_engine.o josekiload.o

SGF = joseki_44.sgf joseki_34.sgf joseki_54.sgf joseki_33.sgf 

# don't build data by default, trips automated build systems
all: lib.a

data: ../joseki19.gtp

../joseki19.gtp: $(SGF)
	@echo -n "" > $@.tmp
	@for f in $(SGF); do \
		printf "\n$$f\n"; \
		./sgfdump $$f >> $@.tmp  ||  exit 1 ; \
	done
	@mv $@.tmp $@
	@echo wrote $@

all: lib.a
lib.a: $(OBJS)


-include ../Makefile.lib
