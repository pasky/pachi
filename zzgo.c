#define DEBUG
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "replay/replay.h"
#include "montecarlo/montecarlo.h"
#include "random/random.h"
#include "t-unit/test.h"
#include "uct/uct.h"
#include "gtp.h"
#include "random.h"
#include "version.h"

int debug_level = 1;
int seed;

int main(int argc, char *argv[])
{
	struct board *b = board_init();
	enum { E_RANDOM, E_REPLAY, E_MONTECARLO, E_UCT } engine = E_UCT;
	char *testfile = NULL;

	seed = time(NULL);

	int opt;
	while ((opt = getopt(argc, argv, "e:d:s:t:")) != -1) {
		switch (opt) {
			case 'e':
				if (!strcasecmp(optarg, "random")) {
					engine = E_RANDOM;
				} else if (!strcasecmp(optarg, "replay")) {
					engine = E_REPLAY;
				} else if (!strcasecmp(optarg, "montecarlo")) {
					engine = E_MONTECARLO;
				} else if (!strcasecmp(optarg, "uct")) {
					engine = E_UCT;
				} else {
					fprintf(stderr, "%s: Invalid -e argument %s\n", argv[0], optarg);
					exit(1);
				}
				break;
			case 'd':
				debug_level = atoi(optarg);
				break;
			case 's':
				seed = atoi(optarg);
				break;
			case 't':
				testfile = strdup(optarg);
				break;
			default: /* '?' */
				fprintf(stderr, "Pachi version %s\n", PACHI_VERSION);
				fprintf(stderr, "Usage: %s [-e random|replay|montecarlo|uct] [-d DEBUG_LEVEL] [-s RANDOM_SEED] [-t FILENAME] [ENGINE_ARGS]\n",
						argv[0]);
				exit(1);
		}
	}

	fast_srandom(seed);
	fprintf(stderr, "Random seed: %d\n", seed);

	char *e_arg = NULL;
	if (optind < argc)
		e_arg = argv[optind];
	struct engine *e;
	switch (engine) {
		case E_RANDOM:
		default:
			e = engine_random_init(e_arg); break;
		case E_REPLAY:
			e = engine_replay_init(e_arg); break;
		case E_MONTECARLO:
			e = engine_montecarlo_init(e_arg); break;
		case E_UCT:
			e = engine_uct_init(e_arg); break;

	}

	if (testfile) {
		unittest(testfile);
		return 0;
	}

	char buf[4096];
	while (fgets(buf, 4096, stdin)) {
		if (DEBUGL(1))
			fprintf(stderr, "IN: %s", buf);
		gtp_parse(b, e, buf);
	}
	return 0;
}
