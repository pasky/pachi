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
#include "engines/josekiplay.h"
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

static void main_loop(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti, time_info_t *ti_default);

char *pachi_exe = NULL;
int   debug_level = 3;
bool  debug_boardprint = true;
long  verbose_logs = 0;
char *forced_ruleset = NULL;
bool  nopassfirst = false;

static char *gtp_port = NULL;
static int   accurate_scoring_wanted = 0;

static void
network_init()
{
#ifdef NETWORK
	int gtp_sock = -1;
	if (gtp_port)		open_gtp_connection(&gtp_sock, gtp_port);
#endif
}

bool
pachi_set_rules(gtp_t *gtp, board_t *b, const char *name)
{
	if (gtp->accurate_scoring &&   /* GnuGo handles japaneses or chinese */
	    b->rules != RULES_JAPANESE && b->rules != RULES_CHINESE)
		die("--accurate-scoring: rules must be japanese or chinese\n");

	return board_set_rules(b, name);
}

static void
accurate_scoring_init(gtp_t *gtp, board_t *b) {
	if (!accurate_scoring_wanted) {
		if (DEBUGL(1)) fprintf(stderr, "Scoring: using mcts (possibly inaccurate)\n");
		return;
	}
	
	if (check_gnugo()) {
		gtp->accurate_scoring = true;
		pachi_set_rules(gtp, b, rules2str(b->rules));  /* recheck rules */
		if (DEBUGL(1)) fprintf(stderr, "Scoring: using gnugo (accurate)\n");
		return;
	}
	
	if (accurate_scoring_wanted > 1)  /* required ? */
		die("Couldn't find gnugo, needed for --accurate-scoring. Aborting.\n");
	else
		if (DEBUGL(1)) warning("WARNING: gnugo not found, using mcts to compute dead stones (possibly inaccurate)\n");
}

static engine_init_t engine_inits[E_MAX] = { NULL };

static void
init()
{
	engine_inits[ E_RANDOM ]      = engine_random_init;
	engine_inits[ E_REPLAY ]      = engine_replay_init;
	engine_inits[ E_PATTERNSCAN ] = engine_patternscan_init;
	engine_inits[ E_PATTERNPLAY ] = engine_patternplay_init;
	engine_inits[ E_JOSEKISCAN ]  = engine_josekiscan_init;
	engine_inits[ E_JOSEKIPLAY ]  = engine_josekiplay_init;
	engine_inits[ E_MONTECARLO ]  = engine_montecarlo_init;
	engine_inits[ E_UCT ]         = engine_uct_init;
#ifdef DISTRIBUTED
	engine_inits[ E_DISTRIBUTED ] = engine_distributed_init;
#endif
#ifdef DCNN
	engine_inits[ E_DCNN ]        = engine_dcnn_init;
#endif
};

void
pachi_engine_init(engine_t *e, int id, board_t *b)
{
	assert(id >= 0 && id < E_MAX);	
	engine_inits[id](e, b);
}

static void
usage()
{
	fprintf(stderr, "Usage: pachi [OPTIONS] [ENGINE_ARGS]\n\n");
	fprintf(stderr,
		"Options: \n"
                "      --compile-flags               show pachi's compile flags \n"
		"  -e, --engine ENGINE               select engine (default uct). Supported engines: \n"
		"                                    uct, dcnn, patternplay, replay, random, montecarlo, distributed \n"
		"  -h, --help                        show usage \n"
		"  -s, --seed RANDOM_SEED            set random seed \n"
		"  -u, --unit-test FILE              run unit tests \n"
		"  -v, --version                     show version \n"
		"      --version=VERSION             version to return to gtp frontend \n"
		"      --name=NAME                   name to return to gtp frontend \n"
		" \n"
		"Gameplay: \n"
		"  -f, --fbook FBOOKFILE             use opening book \n"
		"      --noundo                      undo only allowed for pass \n"
		"  -r, --rules RULESET               rules to use: (default chinese) \n"
		"                                    japanese|chinese|aga|new_zealand|simplified_ing \n"
		"KGS: \n"
		"      --accurate-scoring            use GnuGo to compute dead stones at the end. otherwise expect \n"
		"                                    ~5%% games to be scored incorrectly. recommended for online play \n"
		"  -c, --chatfile FILE               set kgs chatfile \n"
		"      --nopassfirst                 don't pass first \n"
		"      --kgs                         use this when playing on kgs, \n"
		"                                    enables --nopassfirst, and --accurate-scoring if gnugo is found \n"
		"Logs / IO: \n"
		"  -d, --debug-level LEVEL           set debug level \n"
		"  -D                                don't log board diagrams \n"
#ifdef NETWORK
		"  -g, --gtp-port [HOST:]GTP_PORT    read gtp commands from network instead of stdin. \n"
		"                                    listen on given port if HOST not given, otherwise \n"
		"                                    connect to remote host. \n"
		"  -l, --log-port [HOST:]LOG_PORT    log to remote host instead of stderr \n"
#endif
		"  -o  --log-file FILE               log to FILE instead of stderr \n"
		"      --verbose-caffe               enable caffe logging \n"
		" \n"
		"Engine components: \n"
		"      --dcnn,     --nodcnn          dcnn required / disabled \n"
		"      --patterns, --nopatterns      mm patterns required / disabled \n"
		"      --joseki,   --nojoseki        joseki engine required / disabled \n"
		" \n"
#ifdef DCNN
		"Deep learning: \n"
		"      --dcnn=name                   choose which dcnn to load (default detlef) \n"
		"      --dcnn=file                   \n"
		"      --list-dcnns                  show supported networks \n"
		" \n"
#endif
		"Time settings: \n"
		"  -t, --time TIME_SETTINGS          force basic time settings (override kgs/gtp time settings) \n"
		"      --fuseki-time TIME_SETTINGS   specific time settings to use during fuseki \n"
		"      --fuseki MOVES                set fuseki length for --fuseki-time \n"
		"                                    default: 19x19: 10  15x15: 7  9x9: 4 \n"
		" \n"
		"  TIME_SETTINGS: \n"
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
		"Engine args: \n"
		"  Comma separated engine specific options, as in:\n"
		"      pachi --nodcnn threads=8,max_tree_size=3072,pondering \n"
		"  See respective engines for details. Most common options for uct: \n"
		"\n"
		"      max_tree_size=100             use up to 100 Mb of memory for tree search \n"
		"      resign_threshold=0.25         resign if winrate < 25%% (default: 20%%) \n"
		"      reportfreq=1s                 show search progress every second (default: 1000 playouts) \n"
		"      threads=4                     use 4 threads for tree search (default: #cores) \n"
		"      pondering                     think during opponent turn (default: no) \n"
		" \n");
}

static void
show_version(FILE *s)
{
	fprintf(s, "Pachi %s\n", PACHI_VERSION_FULL);
	if (!DEBUGL(2)) return;

	fprintf(s, "git %s\n", PACHI_VERGIT);

	/* Build info */
	char boardsize[32] = "";
#ifdef BOARD_SIZE
	sprintf(boardsize, "[%ix%i]", BOARD_SIZE, BOARD_SIZE);
#endif
	fprintf(s, "%s  %s\n\n", PACHI_VERBUILD, boardsize);
}


#define OPT_FUSEKI_TIME       256
#define OPT_NODCNN            257
#define OPT_DCNN              258
#define OPT_VERBOSE_CAFFE     259
#define OPT_COMPILE_FLAGS     260
#define OPT_NOPASSFIRST       261
#define OPT_PATTERNS          262
#define OPT_NOPATTERNS        263
#define OPT_JOSEKI            264
#define OPT_NOJOSEKI          265
#define OPT_FUSEKI            266
#define OPT_NOUNDO            267
#define OPT_KGS               268
#define OPT_NAME              269
#define OPT_LIST_DCNNS	      270
#define OPT_ACCURATE_SCORING  271

static struct option longopts[] = {
	{ "accurate-scoring",   no_argument,       0, OPT_ACCURATE_SCORING },	
	{ "chatfile",           required_argument, 0, 'c' },
	{ "compile-flags",      no_argument,       0, OPT_COMPILE_FLAGS },
	{ "debug-level",        required_argument, 0, 'd' },
	{ "dcnn",               optional_argument, 0, OPT_DCNN },
	{ "engine",             required_argument, 0, 'e' },
	{ "fbook",              required_argument, 0, 'f' },
	{ "fuseki-time",        required_argument, 0, OPT_FUSEKI_TIME },
	{ "fuseki",             required_argument, 0, OPT_FUSEKI },
#ifdef NETWORK
	{ "gtp-port",           required_argument, 0, 'g' },
	{ "log-port",           required_argument, 0, 'l' },
#endif
	{ "help",               no_argument,       0, 'h' },
	{ "joseki",             no_argument,       0, OPT_JOSEKI },	
	{ "kgs",                no_argument,       0, OPT_KGS },
#ifdef DCNN
	{ "list-dcnns",         no_argument,       0, OPT_LIST_DCNNS },
#endif
	{ "log-file",           required_argument, 0, 'o' },
	{ "name",               required_argument, 0, OPT_NAME },
	{ "nodcnn",             no_argument,       0, OPT_NODCNN },
	{ "noundo",             no_argument,       0, OPT_NOUNDO },
	{ "nojoseki",           no_argument,       0, OPT_NOJOSEKI },
	{ "nopassfirst",        no_argument,       0, OPT_NOPASSFIRST },
	{ "nopatterns",         no_argument,       0, OPT_NOPATTERNS },
	{ "patterns",           no_argument,       0, OPT_PATTERNS },
	{ "rules",              required_argument, 0, 'r' },
	{ "seed",               required_argument, 0, 's' },
	{ "time",               required_argument, 0, 't' },
	{ "unit-test",          required_argument, 0, 'u' },
	{ "verbose-caffe",      no_argument,       0, OPT_VERBOSE_CAFFE },
	{ "version",            optional_argument, 0, 'v' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char *argv[])
{
	init();
	
	pachi_exe = argv[0];
	enum engine_id engine_id = E_UCT;
	time_info_t ti_default = ti_none;
	int  seed = time(NULL) ^ getpid();
	char *testfile = NULL;
	char *log_port = NULL;
	char *chatfile = NULL;
	char *fbookfile = NULL;
	FILE *file = NULL;
	bool verbose_caffe = false;

	setlinebuf(stdout);
	setlinebuf(stderr);

	win_set_pachi_cwd(argv[0]);

	gtp_t maingtp, *gtp = &maingtp;
	gtp_init(gtp);
	
	int opt;
	int option_index;
	/* Leading ':' -> we handle error messages. */
	while ((opt = getopt_long(argc, argv, ":c:e:d:Df:g:hl:o:r:s:t:u:v::", longopts, &option_index)) != -1) {
		switch (opt) {
			case OPT_ACCURATE_SCORING:
				accurate_scoring_wanted = 2; /* required */
				break;
			case 'c':
				chatfile = strdup(optarg);
				break;
			case OPT_COMPILE_FLAGS:
				printf("Compiler:\n%s\n\n", PACHI_COMPILER);
				printf("CFLAGS:\n%s\n\n", PACHI_CFLAGS);
				printf("Command:\n%s\n", PACHI_CC1);
				exit(0);
			case 'e':
				if      (!strcasecmp(optarg, "random"))		engine_id = E_RANDOM;
				else if (!strcasecmp(optarg, "replay"))		engine_id = E_REPLAY;
				else if (!strcasecmp(optarg, "montecarlo"))	engine_id = E_MONTECARLO;
				else if (!strcasecmp(optarg, "uct"))		engine_id = E_UCT;
#ifdef DISTRIBUTED
				else if (!strcasecmp(optarg, "distributed"))	engine_id = E_DISTRIBUTED;
#endif
				else if (!strcasecmp(optarg, "patternscan"))	engine_id = E_PATTERNSCAN;
				else if (!strcasecmp(optarg, "patternplay"))	engine_id = E_PATTERNPLAY;
#ifdef DCNN
				else if (!strcasecmp(optarg, "dcnn"))		engine_id = E_DCNN;
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
				if (optarg)  set_dcnn(optarg);
				require_dcnn();
				break;
			case 'f':
				fbookfile = strdup(optarg);
				break;
#ifdef NETWORK
			case 'g':
				gtp_port = strdup(optarg);
				break;
#endif
			case 'h':
				usage();
				exit(0);
			case OPT_JOSEKI:
				require_joseki();
				break;
			case OPT_KGS:
				gtp->kgs = true;                /* Show engine comment in version. */
				nopassfirst = true;             /* --nopassfirst */
				accurate_scoring_wanted = 1;    /* use gnugo to get dead stones, if possible */
				break;
#ifdef NETWORK
			case 'l':
				log_port = strdup(optarg);
				break;
#endif
#ifdef DCNN
			case OPT_LIST_DCNNS:
				list_dcnns();
				exit(0);
#endif
			case 'o':
				file = fopen(optarg, "w");   if (!file) fail(optarg);
				fclose(file);
				if (!freopen(optarg, "w", stderr))  fail("freopen()");
				setlinebuf(stderr);
				break;
			case OPT_NODCNN:
				disable_dcnn();
				break;
			case OPT_NOUNDO:
				gtp->noundo = true;
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
			case OPT_FUSEKI:
				set_fuseki_moves(atoi(optarg));
				break;
			case OPT_FUSEKI_TIME:
				if (!time_parse(&ti_fuseki, optarg))
					die("%s: Invalid --fuseki-time argument %s\n", argv[0], optarg);
				ti_fuseki.ignore_gtp = true;
				assert(ti_fuseki.period != TT_NULL);
				break;
			case OPT_NAME:
				gtp->custom_name = strdup(optarg);
				break;
			case 'u':
				testfile = strdup(optarg);
				break;
			case OPT_VERBOSE_CAFFE:
				verbose_caffe = true;
				break;
			case 'v':
				if (optarg)  gtp->custom_version = strdup(optarg);
				else         {  show_version(stdout);  exit(0);  }
				break;
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

	board_t *b = board_new(dcnn_default_board_size(), fbookfile);
	if (forced_ruleset) {
		if (!pachi_set_rules(gtp, b, forced_ruleset))  die("Unknown ruleset: %s\n", forced_ruleset);
		if (DEBUGL(1))  fprintf(stderr, "Rules: %s\n", forced_ruleset);
	}
	accurate_scoring_init(gtp, b);

	time_info_t ti[S_MAX];
	ti[S_BLACK] = ti_default;
	ti[S_WHITE] = ti_default;

	chat_init(chatfile);

	char *e_arg = NULL;
	if (optind < argc)	e_arg = argv[optind];
	engine_t e;  engine_init(&e, engine_id, e_arg, b);
	network_init();

	while (1) {
		main_loop(gtp, b, &e, ti, &ti_default);
		if (!gtp_port) break;
		network_init();
	}

	engine_done(&e);
	board_delete(&b);
	chat_done();
	free(testfile);
	free(gtp_port);
	free(log_port);
	free(chatfile);
	free(fbookfile);
	free(forced_ruleset);
	return 0;
}

static void
main_loop(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti, time_info_t *ti_default)
{
	char buf[4096];
	while (fgets(buf, 4096, stdin)) {
		if (DEBUGL(1))  fprintf(stderr, "IN: %s", buf);

		enum parse_code c = gtp_parse(gtp, b, e, ti, buf);

		/* The gtp command is a weak identity check,
		 * close the connection with a wrong peer. */
		if (c == P_UNKNOWN_COMMAND && gtp_port)  return;
		
		if (c == P_ENGINE_RESET) {
			ti[S_BLACK] = *ti_default;
			ti[S_WHITE] = *ti_default;
			if (!e->keep_on_clear)
				engine_reset(e, b);
		}
	}
}

void
pachi_done()
{
	joseki_done();
	prob_dict_done();
	spatial_dict_done();
}
