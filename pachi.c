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
#include "engines/replay.h"
#include "engines/montecarlo.h"
#include "engines/random.h"
#include "engines/patternscan.h"
#include "engines/patternplay.h"
#include "engines/joseki.h"
#include "t-unit/test.h"
#include "uct/uct.h"
#include "distributed/distributed.h"
#include "gtp.h"
#include "chat.h"
#include "timeinfo.h"
#include "random.h"
#include "version.h"
#include "network.h"
#include "uct/tree.h"
#include "dcnn.h"

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
#ifdef DCNN
	E_DCNN,
#endif
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
#ifdef DCNN
	engine_dcnn_init,
#endif
};

static struct engine *init_engine(enum engine_id engine, char *e_arg, struct board *b)
{
	char *arg = e_arg? strdup(e_arg) : e_arg;
	assert(engine < E_MAX);
	struct engine *e = engine_init[engine](arg, b);
	if (arg) free(arg);
	return e;
}

static void usage(char *name)
{
	fprintf(stderr, "Pachi version %s\n", PACHI_VERSION);
	fprintf(stderr, "Usage: %s [OPTIONS] [ENGINE_ARGS]\n\n", name);
	fprintf(stderr,
		"Options: \n"
		"  -c, --chatfile FILE               set kgs chatfile \n"
		"  -d, --debug-level LEVEL           set debug level \n"
		"  -D                                don't log board diagrams \n"
		"  -e, --engine ENGINE               select engine: (default uct) \n"
		"                                    random|replay|montecarlo|uct|distributed|dcnn|patternplay \n"
		"  -f, --fbook FBOOKFILE             use opening book \n"
		"  -g, --gtp-port [HOST:]GTP_PORT    read gtp commands from network instead of stdin. \n"
		"                                    listen on given port if HOST not given, otherwise \n"
		"                                    connect to remote host. \n"
		"  -l, --log-port [HOST:]LOG_PORT    log to remote host instead of stderr \n"
		"  -r, --rules RULESET               rules to use: (default chinese) \n"
		"                                    japanese|chinese|aga|new_zealand|simplified_ing \n"
		"  -s, --seed RANDOM_SEED            set random seed \n"
		"  -t, --time TIME_SETTINGS          force basic time settings (override kgs/gtp time settings) \n"
		"      --fuseki-time TIME_SETTINGS   specific time settings to use during fuseki \n"
		"  -u, --unit-test FILE              run unit tests \n"
		" \n"
		"TIME_SETTINGS: \n"
		"  =SIMS           fixed number of Monte-Carlo simulations per move \n"
		"                  Pachi will play fast on a fast computer, slow on a slow computer, \n"
		"                  but strength will remain the same. \n"
		"  =SIMS:MAX_SIMS  same but allow playing up-to MAX_SIMS simulations if best move is unclear. \n"
		"                  useful to avoid blunders when playing with very low number of simulations. \n"
		"  SECS            fixed number of seconds per move \n"
		"                  Pachi will spend a little less to allow for network latency and other \n"
		"                  unexpected slowdowns. This is the same as one-period japanese byoyomi. \n"
		"  _SECS           absolute time: use fixed number of seconds for the whole game\n"
		" \n"
		"  Examples:       pachi -t =5000            5000 simulations per move \n"
		"                  pachi -t =5000:15000      max 15000 simulations per move \n"
		"                  pachi -t 20               20s per move \n"
		"                  pachi -t _600             10min game, sudden death \n"
		" \n");
}

#define OPT_FUSEKI_TIME 256
static struct option longopts[] = {
	{ "fuseki-time", required_argument, 0, OPT_FUSEKI_TIME },
	{ "chatfile",    required_argument, 0, 'c' },
	{ "debug-level", required_argument, 0, 'd' },
	{ "engine",      required_argument, 0, 'e' },
	{ "fbook",       required_argument, 0, 'f' },
	{ "gtp-port",    required_argument, 0, 'g' },
	{ "log-port",    required_argument, 0, 'l' },
	{ "rules",       required_argument, 0, 'r' },
	{ "seed",        required_argument, 0, 's' },
	{ "time",        required_argument, 0, 't' },
	{ "unit-test",   required_argument, 0, 'u' },
	{ 0, 0, 0, 0 }
};

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
	int option_index;
	while ((opt = getopt_long(argc, argv, "c:e:d:Df:g:l:r:s:t:u:", longopts, &option_index)) != -1) {
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
#ifdef DCNN
				} else if (!strcasecmp(optarg, "dcnn")) {
					engine = E_DCNN;
#endif
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
			case OPT_FUSEKI_TIME:
				if (!time_parse(&ti_fuseki, optarg)) {
					fprintf(stderr, "%s: Invalid --fuseki-time argument %s\n", argv[0], optarg);
					exit(1);
				}
				ti_fuseki.ignore_gtp = true;
				assert(ti_fuseki.period != TT_NULL);
				break;
			case 'u':
				testfile = strdup(optarg);
				break;
			default: /* '?' */
				usage(argv[0]);
				exit(1);
		}
	}

	dcnn_quiet_caffe(argc, argv);
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
					engine_done(e);
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
	engine_done(e);
	chat_done();
	free(testfile);
	free(gtp_port);
	free(log_port);
	free(chatfile);
	free(fbookfile);
	free(ruleset);
	return 0;
}
