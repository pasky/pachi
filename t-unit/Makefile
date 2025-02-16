INCLUDES=-I..

OBJS := test.o

ifeq ($(BOARD_TESTS), 1)
	OBJS += test_undo.o board_regtest.o moggy_regtest.o spatial_regtest.o
endif

all: lib.a
lib.a: $(OBJS)


# Use this to test a josekifix build without having to download the datafiles
# PACHI = ./runpachi --nojosekifix
PACHI = ./runpachi

export PACHI

# Run unit tests

test: FORCE
	@if $(PACHI) --compile-flags | grep -q "BOARD_TESTS"; then  \
		make test_moggy test_spatial test_board; \
	fi

	@if $(PACHI) --compile-flags | grep -q "JOSEKIFIX"; then  \
		make test_external_engine; \
	fi

	./run_tests

	@make test_gtp test_genmove test_vloss test_quiet

test_genmove:
	@echo -n "Testing uct genmove...   "
	@ $(PACHI) -d0 -t =1000 < ../gtp/genmove.gtp  2>pachi.log >/dev/null
	@echo "OK"

test_vloss:
	@echo -n "Testing virtual loss black...    "
	@ if ./test_virtual_loss black; then \
	   echo "OK"; else echo "FAILED"; exit 1;  \
	fi
	@echo -n "Testing virtual loss white...    "
	@ if ./test_virtual_loss white; then \
	   echo "OK"; else echo "FAILED"; exit 1;  \
        fi

test_quiet:
	@echo -n "Testing quiet mode...    "
	@if  grep -q '.' < pachi.log ; then \
		echo "FAILED:";  cat pachi.log;  exit 1;  else  echo "OK"; \
	fi

test_board: FORCE
	@if ! $(PACHI) --compile-flags | grep -q "BOARD_TESTS"; then  \
		echo "Looks like board tests are missing, try building with BOARD_TESTS=1"; exit 1;  \
	fi

	@echo -n "Testing board logic didn't change...   "
	@  $(PACHI) -d0 < regtest.gtp  2>regtest.out  >/dev/null
	@if bzcmp regtest.out regtest.ref.bz2  >/dev/null; then \
	   echo "OK"; else  echo "FAILED"; exit 1;  fi

	@$(PACHI) -d2 -u board_undo.t

test_moggy: FORCE
	@echo -n "Testing moggy logic didn't change...   "
	@  echo "tunit moggy_regtest" | $(PACHI) -d0 2>moggy.out  >/dev/null
	@if bzcmp moggy.out moggy.ref.bz2  >/dev/null; then \
	   echo "OK"; else  echo "FAILED"; exit 1;  fi

test_spatial: FORCE
	@echo -n "Testing spatials didn't change...   "
	@  $(PACHI) -d0 < spatial.gtp  2>spatial.out  >/dev/null
	@if bzcmp spatial.out spatial.ref.bz2  >/dev/null; then \
	   echo "OK"; else  echo "FAILED"; exit 1;  fi

test_gtp: FORCE
	@echo "Testing gtp is sane...   "
	@if $(PACHI) --compile-flags | grep -q "DCNN"; then  \
		cp gtp_test.gtp tmp.gtp;  \
	 else \
		cat gtp_test.gtp | grep -v dcnn >tmp.gtp; \
	 fi
        # Check test suite is not missing some commands...
	@echo list_commands | $(PACHI) -d0 | sed -e 's/^= //' | \
           while read f; do  if ! grep -q "^\(# *\|\)$$f" tmp.gtp  ; then \
               echo "t-unit/gtp_test.gtp: command $$f not tested, fixme !"; exit 1 ; fi; done
	@if ./gtp_check $(PACHI) -t =1000 -o /dev/null < tmp.gtp;  then \
	   echo "OK"; else  echo "FAILED"; exit 1;  fi

# Test josekifix external engine sync  (needs gnugo)
test_external_engine:
	@gnugo --version >/dev/null 2>&1 || ( echo "gnugo missing or not in path, aborting." ; exit 1 )
	@gnugo --version | head -1 | grep -q "GNU Go 3.8" || ( echo "need gnugo 3.8, aborting." ; exit 1 )
	@echo -n "Testing josekifix external engine sync...   "
	@cat external_engine.gtp | $(PACHI) -d4 --josekifix --external-joseki-engine 'gnugo --mode gtp' 2>&1 >/dev/null  | \
          grep 'external engine' > external_engine.out
	@if cmp external_engine.out external_engine.ref >/dev/null; then \
	   echo "OK"; else  echo "FAILED"; exit 1;  fi

FORCE:

-include ../Makefile.lib
