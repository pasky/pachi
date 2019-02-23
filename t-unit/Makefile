INCLUDES=-I..

OBJS := test.o

ifeq ($(BOARD_TESTS), 1)
	OBJS += test_undo.o board_regtest.o moggy_regtest.o spatial_regtest.o
endif

all: lib.a
lib.a: $(OBJS)

# Run unit tests
test: FORCE
	@if ../pachi --compile-flags | grep -q "BOARD_TESTS"; then  \
		make test_board test_moggy test_spatial; \
	fi

	@make test_gtp
	./run_tests

	@echo -n "Testing uct genmove...   "
	@ ../pachi -d0 -t =1000 < ../gtp/genmove.gtp  2>pachi.log >/dev/null
	@echo "OK"

	@echo -n "Testing quiet mode...    "
	@if  grep -q '.' < pachi.log ; then \
		echo "FAILED:";  cat pachi.log;  exit 1;  else  echo "OK"; \
	fi

test_board: FORCE
	@if ! ../pachi --compile-flags | grep -q "BOARD_TESTS"; then  \
		echo "Looks like board tests are missing, try building with BOARD_TESTS=1"; exit 1;  \
	fi

	@echo -n "Testing board logic didn't change...   "
	@  ../pachi -d0 < regtest.gtp  2>regtest.out  >/dev/null
	@if bzcmp regtest.out regtest.ref.bz2  >/dev/null; then \
	   echo "OK"; else  echo "FAILED"; exit 1;  fi

	@../pachi -d2 -u board_undo.t

test_moggy: FORCE
	@echo -n "Testing moggy logic didn't change...   "
	@  echo "tunit moggy_regtest" | ../pachi -d0 2>moggy.out  >/dev/null
	@if bzcmp moggy.out moggy.ref.bz2  >/dev/null; then \
	   echo "OK"; else  echo "FAILED"; exit 1;  fi

test_spatial: FORCE
	@echo -n "Testing spatials didn't change...   "
	@  ../pachi -d0 < spatial.gtp  2>spatial.out  >/dev/null
	@if bzcmp spatial.out spatial.ref.bz2  >/dev/null; then \
	   echo "OK"; else  echo "FAILED"; exit 1;  fi

test_gtp: FORCE
	@echo "Testing gtp is sane...   "
	@if ../pachi --compile-flags | grep -q "DCNN"; then  \
		cp gtp_test.gtp tmp.gtp;  \
	 else \
		cat gtp_test.gtp | grep -v dcnn >tmp.gtp; \
	 fi
        # Check test suite is not missing some commands...
	@echo list_commands | ../pachi -d0 | sed -e 's/^= //' | \
           while read f; do  if ! grep -q "^\(# *\|\)$$f" tmp.gtp  ; then \
               echo "t-unit/gtp_test.gtp: command $$f not tested, fixme !"; exit 1 ; fi; done
	@if ./gtp_check ../pachi -t =1000 -o /dev/null < tmp.gtp;  then \
	   echo "OK"; else  echo "FAILED"; exit 1;  fi

FORCE:

-include ../Makefile.lib
