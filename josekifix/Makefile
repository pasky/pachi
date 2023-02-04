INCLUDES=-I..

OBJS := fuseki.o josekifix.o josekifixscan_engine.o

SGF = josekifix.sgf

all: ../josekifix.gtp lib.a
lib.a: $(OBJS)

../josekifix.gtp: $(SGF)
	@echo -n "" > $@.tmp
	@for f in $(SGF); do \
		printf "\n$$f\n"; \
		./sgfdump $$f >> $@.tmp  ||  exit 1 ; \
	done
	@mv $@.tmp $@
	@echo wrote $@


-include ../Makefile.lib
