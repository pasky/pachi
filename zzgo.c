#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "montecarlo/montecarlo.h"
#include "montecasino/montecasino.h"
#include "random/random.h"
#include "uct/uct.h"
#include "gtp.h"
#include "random.h"
#include "version.h"

int debug_level = 1;
int seed;

int main(int argc, char *argv[])
{
	struct board *b = board_init();
	enum { E_RANDOM, E_MONTECARLO, E_MONTECASINO, E_UCT } engine = E_UCT;

	seed = time(NULL);

	int opt;
	while ((opt = getopt(argc, argv, "e:d:s:")) != -1) {
		switch (opt) {
			case 'e':
				if (!strcasecmp(optarg, "random")) {
					engine = E_RANDOM;
				} else if (!strcasecmp(optarg, "montecarlo")) {
					engine = E_MONTECARLO;
				} else if (!strcasecmp(optarg, "montecasino")) {
					engine = E_MONTECASINO;
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
			default: /* '?' */
				fprintf(stderr, "Pachi version %s\n", PACHI_VERSION);
				fprintf(stderr, "Usage: %s [-e random|montecarlo|montecasino] [-d DEBUG_LEVEL] [-s RANDOM_SEED] [ENGINE_ARGS]\n",
						argv[0]);
				exit(1);
		}
	}

	fast_srandom(seed);
	if (DEBUGL(0))
		fprintf(stderr, "Random seed: %d", seed);

	char *e_arg = NULL;
	if (optind < argc)
		e_arg = argv[optind];
	struct engine *e;
	switch (engine) {
		case E_RANDOM:
		default:
			e = engine_random_init(e_arg); break;
		case E_MONTECARLO:
			e = engine_montecarlo_init(e_arg); break;
		case E_MONTECASINO:
			e = engine_montecasino_init(e_arg); break;
		case E_UCT:
			e = engine_uct_init(e_arg); break;

	}

	char buf[256];
	while (fgets(buf, 256, stdin)) {
		if (DEBUGL(1))
			fprintf(stderr, "IN: %s", buf);
		gtp_parse(b, e, buf);
		if (DEBUGL(1))
			board_print(b, stderr);
	}
	return 0;
}
