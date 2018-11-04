#define DEBUG
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "board.h"
#include "pachi.h"
#include "debug.h"
#include "engine.h"
#include "engines/replay.h"
#include "engines/montecarlo.h"
#include "engines/random.h"
#include "engines/patternscan.h"
#include "engines/patternplay.h"
#include "engines/josekiscan.h"
#include "engines/dcnn.h"
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
#include "fifo.h"
#include "dcnn.h"
#include "caffe.h"
#include "pattern.h"
#include "patternsp.h"
#include "patternprob.h"
#include "joseki.h"

char *pachi_exe = NULL;
int debug_level = 3;
bool debug_boardprint = true;
long verbose_logs = 0;
int seed;
char *forced_ruleset = NULL;
bool nopassfirst = false;

enum engine_id {
	E_RANDOM,
	E_REPLAY,
	E_PATTERNSCAN,
	E_PATTERNPLAY,
	E_MONTECARLO,
	E_UCT,
#ifdef DISTRIBUTED
	E_DISTRIBUTED,
#endif
#ifdef DCNN
	E_DCNN,
#endif
	E_MAX,
};

static engine_init_t engine_init[E_MAX] = {
	engine_random_init,
	engine_replay_init,
	engine_patternscan_init,
	engine_patternplay_init,
	engine_montecarlo_init,
	engine_uct_init,
#ifdef DISTRIBUTED
	engine_distributed_init,
#endif
#ifdef DCNN
	engine_dcnn_init,
#endif
};

static struct engine *
init_engine(enum engine_id engine, char *e_arg, struct board *b)
{
	char *arg = e_arg? strdup(e_arg) : e_arg;
	assert(engine < E_MAX);
	struct engine *e = engine_init[engine](arg, b);
	if (arg) free(arg);
	return e;
}

static void
usage()
{
	fprintf(stderr, "Usage: pachi [OPTIONS] [ENGINE_ARGS]\n\n");
	fprintf(stderr,
		"Options: \n"
		"  -c, --chatfile FILE               set kgs chatfile \n"
                "      --compile-flags               show pachi's compile flags \n"
		"  -d, --debug-level LEVEL           set debug level \n"
		"  -D                                don't log board diagrams \n"
		"      --nopassfirst                 don't pass first (needed for kgs) \n"
		"  -e, --engine ENGINE               select engine (default uct). Supported engines: \n"
		"                                    uct, dcnn, patternplay, replay, random, montecarlo, distributed \n"
		"  -f, --fbook FBOOKFILE             use opening book \n"
		"  -g, --gtp-port [HOST:]GTP_PORT    read gtp commands from network instead of stdin. \n"
		"                                    listen on given port if HOST not given, otherwise \n"
		"                                    connect to remote host. \n"
		"  -h, --help                        show usage \n"
		"      --kgs                         turn on kgs-specific behavior (currently --nopassfirst) \n"
		"  -l, --log-port [HOST:]LOG_PORT    log to remote host instead of stderr \n"
		"  -o  --log-file FILE               log to FILE instead of stderr \n"
		"  -r, --rules RULESET               rules to use: (default chinese) \n"
		"                                    japanese|chinese|aga|new_zealand|simplified_ing \n"
		"  -s, --seed RANDOM_SEED            set random seed \n"
		"  -t, --time TIME_SETTINGS          force basic time settings (override kgs/gtp time settings) \n"
		"      --fuseki-time TIME_SETTINGS   specific time settings to use during fuseki \n"
		"  -u, --unit-test FILE              run unit tests \n"
		"      --verbose-caffe               enable caffe logging \n"
		"  -v, --version                     show version \n"
		" \n"
		"Engine components: \n"
		"      --dcnn,     --nodcnn          dcnn required / disabled \n"
		"      --patterns, --nopatterns      mm patterns required / disabled \n"
		"      --joseki,   --nojoseki        joseki engine required / disabled \n"
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

static void
show_version(FILE *s)
{
	fprintf(s, "Pachi version %s\n", PACHI_VERSION);
	if (!DEBUGL(2)) return;

	fprintf(s, "git %s\n", PACHI_VERGIT);

	/* Build info */
	char boardsize[32] = "";
#ifdef BOARD_SIZE
	sprintf(boardsize, "[%ix%i]", BOARD_SIZE, BOARD_SIZE);
#endif
	fprintf(s, "%s  %s\n\n", PACHI_VERBUILD, boardsize);
}

#define OPT_FUSEKI_TIME   256
#define OPT_NODCNN        257
#define OPT_DCNN          258
#define OPT_VERBOSE_CAFFE 259
#define OPT_COMPILE_FLAGS 260
#define OPT_NOPASSFIRST   261
#define OPT_PATTERNS      262
#define OPT_NOPATTERNS    263
#define OPT_JOSEKI        264
#define OPT_NOJOSEKI      265
static struct option longopts[] = {
	{ "fuseki-time", required_argument, 0, OPT_FUSEKI_TIME },
	{ "chatfile",    required_argument, 0, 'c' },
	{ "compile-flags", no_argument,     0, OPT_COMPILE_FLAGS },
	{ "debug-level", required_argument, 0, 'd' },
	{ "dcnn",        no_argument,       0, OPT_DCNN },
	{ "engine",      required_argument, 0, 'e' },
	{ "fbook",       required_argument, 0, 'f' },
	{ "joseki",      no_argument,       0, OPT_JOSEKI },
	{ "gtp-port",    required_argument, 0, 'g' },
	{ "help",        no_argument,       0, 'h' },
	{ "kgs",         no_argument,       0, OPT_NOPASSFIRST },
	{ "log-file",    required_argument, 0, 'o' },
	{ "log-port",    required_argument, 0, 'l' },
	{ "nodcnn",      no_argument,       0, OPT_NODCNN },
	{ "nojoseki",    no_argument,       0, OPT_NOJOSEKI },
	{ "nopassfirst", no_argument,       0, OPT_NOPASSFIRST },
	{ "nopatterns",  no_argument,       0, OPT_NOPATTERNS },
	{ "patterns",    no_argument,       0, OPT_PATTERNS },
	{ "rules",       required_argument, 0, 'r' },
	{ "seed",        required_argument, 0, 's' },
	{ "time",        required_argument, 0, 't' },
	{ "unit-test",   required_argument, 0, 'u' },
	{ "verbose-caffe", no_argument,     0, OPT_VERBOSE_CAFFE },
	{ "version",     no_argument,       0, 'v' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char *argv[])
{
	pachi_exe = argv[0];
	enum engine_id engine = E_UCT;
	struct time_info ti_default = { .period = TT_NULL };	
	char *testfile = NULL;
	char *gtp_port = NULL;
	char *log_port = NULL;
	int gtp_sock = -1;
	char *chatfile = NULL;
	char *fbookfile = NULL;
	FILE *file = NULL;
	bool verbose_caffe = false;

	setlinebuf(stdout);
	setlinebuf(stderr);

	win_set_pachi_cwd(argv[0]);
	
	seed = time(NULL) ^ getpid();

	int opt;
	int option_index;
	/* Leading ':' -> we handle error messages. */
	while ((opt = getopt_long(argc, argv, ":c:e:d:Df:g:hl:o:r:s:t:u:v", longopts, &option_index)) != -1) {
		switch (opt) {
			case 'c':
				chatfile = strdup(optarg);
				break;
			case OPT_COMPILE_FLAGS:
				printf("Compiler:\n%s\n\n", PACHI_COMPILER);
				printf("CFLAGS:\n%s\n\n", PACHI_CFLAGS);
				printf("Command:\n%s\n", PACHI_CC1);
				exit(0);
			case 'e':
				if      (!strcasecmp(optarg, "random"))		engine = E_RANDOM;
				else if (!strcasecmp(optarg, "replay"))		engine = E_REPLAY;
				else if (!strcasecmp(optarg, "montecarlo"))	engine = E_MONTECARLO;
				else if (!strcasecmp(optarg, "uct"))		engine = E_UCT;
#ifdef DISTRIBUTED
				else if (!strcasecmp(optarg, "distributed"))	engine = E_DISTRIBUTED;
#endif
				else if (!strcasecmp(optarg, "patternscan"))	engine = E_PATTERNSCAN;
				else if (!strcasecmp(optarg, "patternplay"))	engine = E_PATTERNPLAY;
#ifdef DCNN
				else if (!strcasecmp(optarg, "dcnn"))		engine = E_DCNN;
#endif
				else die("%s: Invalid -e argument %s\n", argv[0], optarg);
				break;
			case 'd':
				debug_level = atoi(optarg);
				break;
			case 'D':
				debug_boardprint = false;
				break;
			case OPT_DCNN:
				require_dcnn();
				break;
			case 'f':
				fbookfile = strdup(optarg);
				break;
			case 'g':
				gtp_port = strdup(optarg);
				break;
			case 'h':
				usage();
				exit(0);
			case OPT_JOSEKI:
				require_joseki();
				break;
			case 'l':
				log_port = strdup(optarg);
				break;
			case 'o':
				file = fopen(optarg, "w");   if (!file) fail(optarg);
				fclose(file);
				if (!freopen(optarg, "w", stderr))  fail("freopen()");
				setlinebuf(stderr);
				break;
			case OPT_NODCNN:
				disable_dcnn();
				break;
			case OPT_NOJOSEKI:
				disable_joseki();
				break;
			case OPT_NOPASSFIRST:
				nopassfirst = true;
				break;
			case OPT_NOPATTERNS:
				disable_patterns();
				break;
			case OPT_PATTERNS:
				require_patterns();
				break;
			case 'r':
				forced_ruleset = strdup(optarg);
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
				if (!time_parse(&ti_default, optarg))
					die("%s: Invalid -t argument %s\n", argv[0], optarg);
				ti_default.ignore_gtp = true;
				assert(ti_default.period != TT_NULL);
				break;
			case OPT_FUSEKI_TIME:
				if (!time_parse(&ti_fuseki, optarg))
					die("%s: Invalid --fuseki-time argument %s\n", argv[0], optarg);
				ti_fuseki.ignore_gtp = true;
				assert(ti_fuseki.period != TT_NULL);
				break;
			case 'u':
				testfile = strdup(optarg);
				break;
			case OPT_VERBOSE_CAFFE:
				verbose_caffe = true;
				break;
			case 'v':
				show_version(stdout);
				exit(0);
			case ':':
				die("%s: Missing argument\n"
				    "Try 'pachi --help' for more information.\n", argv[optind-1]);
			default: /* '?' */
				die("Invalid argument: %s\n"
				    "Try 'pachi --help' for more information.\n", argv[optind-1]);
		}
	}

	fast_srandom(seed);
	
	if (!verbose_caffe)      quiet_caffe(argc, argv);
	if (log_port)            open_log_port(log_port);
	if (testfile)		 return unit_test(testfile);
	if (DEBUGL(0))           show_version(stderr);
	if (getenv("DATA_DIR"))
		if (DEBUGL(1))   fprintf(stderr, "Using data dir %s\n", getenv("DATA_DIR"));
	if (DEBUGL(2))	         fprintf(stderr, "Random seed: %d\n", seed);
	fifo_init();

	struct board *b = board_new(19 + 2, fbookfile);
	if (forced_ruleset && !board_set_rules(b, forced_ruleset))  die("Unknown ruleset: %s\n", forced_ruleset);

	struct time_info ti[S_MAX];
	ti[S_BLACK] = ti_default;
	ti[S_WHITE] = ti_default;

	chat_init(chatfile);

	char *e_arg = NULL;
	if (optind < argc)	e_arg = argv[optind];
	struct engine *e = init_engine(engine, e_arg, b);

	if (gtp_port)		open_gtp_connection(&gtp_sock, gtp_port);

	for (;;) {
		char buf[4096];
		while (fgets(buf, 4096, stdin)) {
			if (DEBUGL(1))  fprintf(stderr, "IN: %s", buf);

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
	free(forced_ruleset);
	return 0;
}

void
pachi_done()
{
	joseki_done();
	prob_dict_done();
	spatial_dict_done();
}
