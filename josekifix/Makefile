INCLUDES=-I..

OBJS = fuseki.o josekifix.o josekifix_engine.o josekifixload.o special_checks.o
SGF  = josekifix.sgf fusekifix.sgf

# don't build data by default, trips automated build systems
all: lib.a

data: ../josekifix.gtp

# update database: get latest josekifix.sgf from github, and regen josekifix.gtp
update: FORCE
	./update_database
	@make data


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
