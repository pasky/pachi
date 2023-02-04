INCLUDES=-I..

OBJS = fuseki.o josekifix.o josekifixscan_engine.o special_checks.o
SGF  = josekifix.sgf fusekifix.sgf

all: lib.a data

data: ../josekifix.gtp


# Generate josekifix.gtp from josekifix.sgf
../josekifix.gtp: $(SGF)
	@echo -n "" > $@.tmp
	@for f in $(SGF); do \
		echo "$$f"; \
		./sgfdump $$f >> $@.tmp  ||  exit 1 ; \
	done
	@mv $@.tmp $@
	@echo wrote $@

lib.a: $(OBJS)

FORCE:


-include ../Makefile.lib
