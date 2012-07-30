#define DEBUG
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "replay/replay.h"
#include "montecarlo/montecarlo.h"
#include "random/random.h"
#include "patternscan/patternscan.h"
#include "patternplay/patternplay.h"
#include "joseki/joseki.h"
#include "t-unit/test.h"
#include "uct/uct.h"
#include "distributed/distributed.h"
#include "gtp.h"
#include "chat.h"
#include "timeinfo.h"
#include "random.h"
#include "version.h"
#include "network.h"

int debug_level = 3;
bool debug_boardprint = true;
long verbose_logs = 0;
int seed;


enum engine_id {
	E_RANDOM,
	E_REPLAY,
	E_PATTERNSCAN,
	E_PATTERNPLAY,
	E_MONTECARLO,
	E_UCT,
	E_DISTRIBUTED,
	E_JOSEKI,
	E_MAX,
};

static struct engine *(*engine_init[E_MAX])(char *arg, struct board *b) = {
	engine_random_init,
	engine_replay_init,
	engine_patternscan_init,
	engine_patternplay_init,
	engine_montecarlo_init,
	engine_uct_init,
	engine_distributed_init,
	engine_joseki_init,
};

static struct engine *init_engine(enum engine_id engine, char *e_arg, struct board *b)
{
	char *arg = e_arg? strdup(e_arg) : e_arg;
	assert(engine < E_MAX);
	struct engine *e = engine_init[engine](arg, b);
	if (arg) free(arg);
	return e;
}

static void done_engine(struct engine *e)
{
	if (e->done) e->done(e);
	if (e->data) free(e->data);
	free(e);
}

static void usage(char *name)
{
	fprintf(stderr, "Pachi version %s\n", PACHI_VERSION);
	fprintf(stderr, "Usage: %s [-e random|replay|montecarlo|uct|distributed]\n"
		" [-d DEBUG_LEVEL] [-D] [-r RULESET] [-s RANDOM_SEED] [-t TIME_SETTINGS] [-u TEST_FILENAME]\n"
		" [-g [HOST:]GTP_PORT] [-l [HOST:]LOG_PORT] [-f FBOOKFILE] [ENGINE_ARGS]\n", name);
}

int main(int argc, char *argv[])
{
	enum engine_id engine = E_UCT;
	struct time_info ti_default = { .period = TT_NULL };
	char *testfile = NULL;
	char *gtp_port = NULL;
	char *log_port = NULL;
	int gtp_sock = -1;
	char *chatfile = NULL;
	char *fbookfile = NULL;
	char *ruleset = NULL;

	seed = time(NULL) ^ getpid();

	int opt;
	while ((opt = getopt(argc, argv, "c:e:d:Df:g:l:r:s:t:u:")) != -1) {
		switch (opt) {
			case 'c':
				chatfile = strdup(optarg);
				break;
			case 'e':
				if (!strcasecmp(optarg, "random")) {
					engine = E_RANDOM;
				} else if (!strcasecmp(optarg, "replay")) {
					engine = E_REPLAY;
				} else if (!strcasecmp(optarg, "montecarlo")) {
					engine = E_MONTECARLO;
				} else if (!strcasecmp(optarg, "uct")) {
					engine = E_UCT;
				} else if (!strcasecmp(optarg, "distributed")) {
					engine = E_DISTRIBUTED;
				} else if (!strcasecmp(optarg, "patternscan")) {
					engine = E_PATTERNSCAN;
				} else if (!strcasecmp(optarg, "patternplay")) {
					engine = E_PATTERNPLAY;
				} else if (!strcasecmp(optarg, "joseki")) {
					engine = E_JOSEKI;
				} else {
					fprintf(stderr, "%s: Invalid -e argument %s\n", argv[0], optarg);
					exit(1);
				}
				break;
			case 'd':
				debug_level = atoi(optarg);
				break;
			case 'D':
				debug_boardprint = false;
				break;
			case 'f':
				fbookfile = strdup(optarg);
				break;
			case 'g':
				gtp_port = strdup(optarg);
				break;
			case 'l':
				log_port = strdup(optarg);
				break;
			case 'r':
				ruleset = strdup(optarg);
				break;
			case 's':
				seed = atoi(optarg);
				break;
			case 't':
				/* Time settings to follow; if specified,
				 * GTP time information is ignored. Useful
				 * e.g. when you want to force your bot to
				 * play weaker while giving the opponent
				 * reasonable time to play, or force play
				 * by number of simulations in timed games. */
				/* Please see timeinfo.h:time_parse()
				 * description for syntax details. */
				if (!time_parse(&ti_default, optarg)) {
					fprintf(stderr, "%s: Invalid -t argument %s\n", argv[0], optarg);
					exit(1);
				}
				ti_default.ignore_gtp = true;
				assert(ti_default.period != TT_NULL);
				break;
			case 'u':
				testfile = strdup(optarg);
				break;
			default: /* '?' */
				usage(argv[0]);
				exit(1);
		}
	}

	if (log_port)
		open_log_port(log_port);

	fast_srandom(seed);
	if (DEBUGL(0))
		fprintf(stderr, "Random seed: %d\n", seed);

	struct board *b = board_init(fbookfile);
	if (ruleset) {
		if (!board_set_rules(b, ruleset)) {
			fprintf(stderr, "Unknown ruleset: %s\n", ruleset);
			exit(1);
		}
	}

	struct time_info ti[S_MAX];
	ti[S_BLACK] = ti_default;
	ti[S_WHITE] = ti_default;

	chat_init(chatfile);

	char *e_arg = NULL;
	if (optind < argc)
		e_arg = argv[optind];
	struct engine *e = init_engine(engine, e_arg, b);

	if (testfile) {
		unittest(testfile);
		return 0;
	}

	if (gtp_port) {
		open_gtp_connection(&gtp_sock, gtp_port);
	}

	for (;;) {
		char buf[4096];
		while (fgets(buf, 4096, stdin)) {
			if (DEBUGL(1))
				fprintf(stderr, "IN: %s", buf);

			enum parse_code c = gtp_parse(b, e, ti, buf);
			if (c == P_ENGINE_RESET) {
				ti[S_BLACK] = ti_default;
				ti[S_WHITE] = ti_default;
				if (!e->keep_on_clear) {
					b->es = NULL;
					done_engine(e);
					e = init_engine(engine, e_arg, b);
				}
			} else if (c == P_UNKNOWN_COMMAND && gtp_port) {
				/* The gtp command is a weak identity check,
				 * close the connection with a wrong peer. */
				break;
			}
		}
		if (!gtp_port) break;
		open_gtp_connection(&gtp_sock, gtp_port);
	}
	done_engine(e);
	chat_done();
	return 0;
}
