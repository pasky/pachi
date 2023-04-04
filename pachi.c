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
#include "engines/external.h"
#include "engines/dcnn.h"
#include "pattern/patternscan_engine.h"
#include "pattern/pattern_engine.h"
#include "joseki/joseki_engine.h"
#include "joseki/josekiscan_engine.h"
#include "josekifix/josekifixscan_engine.h"
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
#include "pattern/pattern.h"
#include "pattern/spatial.h"
#include "pattern/prob.h"
#include "joseki/joseki.h"
#include "josekifix/josekifix.h"

/* Main options */
static pachi_options_t main_options = { 0, };
const  pachi_options_t *pachi_options() {  return &main_options;  }

char *pachi_exe = NULL;

int   debug_level = 3;
int   saved_debug_level;
bool  debug_boardprint = true;
long  verbose_logs = 0;

static void main_loop(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti, time_info_t *ti_default, char *gtp_port);

typedef struct {
	int		id;
	char	       *name;
	engine_init_t	init;
	bool		show;
} engine_map_t;

/* Must match order in engine.h */
engine_map_t engines[] = {
	{ E_UCT,		"uct",            uct_engine_init,            1 },
#ifdef DCNN
	{ E_DCNN,		"dcnn",           dcnn_engine_init,           1 },
#endif
	{ E_PATTERN,		"pattern",        pattern_engine_init,        1 },
	{ E_PATTERNSCAN,	"patternscan",    patternscan_engine_init,    0 },
	{ E_JOSEKI,		"joseki",         joseki_engine_init,         1 },
	{ E_JOSEKISCAN,		"josekiscan",     josekiscan_engine_init,     0 },
#ifdef JOSEKIFIX
	{ E_JOSEKIFIXSCAN,	"josekifixscan",  josekifixscan_engine_init,  0 },
#endif
	{ E_RANDOM,		"random",         random_engine_init,         1 },
	{ E_REPLAY,		"replay",         replay_engine_init,         1 },
	{ E_MONTECARLO,		"montecarlo",     montecarlo_engine_init,     1 },
#ifdef DISTRIBUTED
	{ E_DISTRIBUTED,	"distributed",    distributed_engine_init,    1 },
#endif
#ifdef JOSEKIFIX
	{ E_EXTERNAL,		"external",       external_engine_init,       0 },
#endif

/* Alternative names */
	{ E_PATTERN,		"patternplay",    pattern_engine_init,        1 },  /* backwards compatibility */
	{ E_JOSEKI,		"josekiplay",     joseki_engine_init,         1 },
	
	{ 0, 0, 0, 0 }
};

static enum engine_id
engine_name_to_id(const char *name)
{
	for (int i = 0; engines[i].name; i++)
		if (!strcmp(name, engines[i].name))
			return engines[i].id;
	return E_MAX;
}

static char*
supported_engines(bool show_all)
{
	static_strbuf(buf, 512);
	for (int i = 0; i < E_MAX; i++)  /* Don't list alt names */
		if (show_all || engines[i].show)
			strbuf_printf(buf, "%s%s", engines[i].name, (engines[i+1].name ? ", " : ""));
	return buf->str;
}

static void
pachi_init(int argc, char *argv[])
{
	setlinebuf(stdout);
	setlinebuf(stderr);
	
	pachi_exe = argv[0];
	win_set_pachi_cwd(argv[0]);
	
	/* Check engine list is sane. */
	for (int i = 0; i < E_MAX; i++)
		assert(engines[i].name && engines[i].id == i);
};

void
pachi_engine_init(engine_t *e, int id, board_t *b)
{
	assert(id >= 0 && id < E_MAX);	
	engines[id].init(e, b);
}

static void
usage_smart_pass()
{
	fprintf(stderr,
		"[ smart pass ]                (!! ok only if players can negotiate dead stones  !!)\n"
		"\n"
		"By default Pachi is fairly pedantic at the end of the game and will refuse to pass \n"
		"until everything is nice and clear to him. This can take some moves depending on the \n"
		"situation if there are unclear groups. Guessing allows more user-friendly behavior, \n"
		"passing earlier without having to clarify everything. Under japanese rules this can \n"
		"also prevent him from losing the game if clarifying would cost too many points. \n"
		"\n"
		"Even though Pachi will only guess won positions there is a possibility of getting dead \n"
		"group status wrong, so only ok if game setup asks players for dead stones and game can \n"
		"resume in case of disagreement (auto-scored games like on ogs for example are definitely \n"
		"not ok). \n"
		"\n"
		"Basically: \n"
		"- direct interactive play           : ok \n"
		"- any kind of automated/online play : no ! \n"
		"- except online play on kgs         : ok   (enabled automatically) \n"
		" \n"
		"So off by default except when playing japanese on kgs. \n"
		"If unsure don't use. \n");
}

static void
usage(char *arg)
{
	if (arg) {
		if (!strcmp(arg, "smart-pass") ||
		    !strcmp(arg, "guess-unclear"))  usage_smart_pass();
		else    die("unknown help topic '%s'\n", arg);
		return;
	}
	
	fprintf(stderr, "Usage: pachi [OPTIONS] [ENGINE_ARGS...]\n\n");
	fprintf(stderr,
		"Options: \n"
                "      --compile-flags               show pachi's compile flags \n"
		"  -e, --engine ENGINE               select engine (default uct). Supported engines: \n");
	fprintf(stderr,
		"                                    %s \n", supported_engines(false));
	fprintf(stderr,
		"  -h, --help                        show usage \n"
		"  -s, --seed RANDOM_SEED            set random seed \n"
		"  -u, --unit-test FILE              run unit tests \n"
		"  -v, --version                     show version \n"
		"      --version=VERSION             version to return to gtp frontend \n"
		"      --name=NAME                   name to return to gtp frontend \n"
		" \n"
		"Gameplay: \n"
		"  -f, --fbook FBOOKFILE             use opening book \n"
		"      --smart-pass, --guess-unclear more user-friendly pass behavior (dangerous) \n"
		"                                    see 'pachi --help smart-pass' for details \n"
		"      --noundo                      undo only allowed for pass \n"
		"  -r, --rules RULESET               rules to use: (default chinese) \n"
		"                                    japanese|chinese|aga|new_zealand|simplified_ing \n"
		"KGS: \n"
		"      --kgs                         use this when playing on kgs, \n"
		"  -c, --chatfile FILE               set kgs chatfile \n"
		"      --kgs-chat                    enable kgs-chat cmd (kgsGtp 3.5.11 only, crashes 3.5.20+) \n"
		"      --nopassfirst                 don't pass first when playing chinese \n"
		" \n"
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
		"                                    guides tree search.                (default: enabled) \n"
		"      --joseki,   --nojoseki        (nodcnn) joseki module required / disabled \n"
#ifdef JOSEKIFIX
		"      --josekifix, --nojosekifix    (dcnn)   joseki fixes required / disabled \n"
		"                                    fixes for joseki/fuseki lines that dcnn plays poorly. \n"
		"                                    requires external engine to act as joseki engine \n"
		"                                    (see --external-joseki-engine)     (default: enabled) \n"
		"      --external-joseki-engine CMD  joseki engine for josekifix module (default: katago) \n"
#endif
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
		"TIME_SETTINGS: \n"
		"  =SIMS           fixed number of Monte-Carlo simulations per move \n"
		"                  Pachi will play fast on a fast computer, slow on a slow computer, \n"
		"                  but strength will remain the same. \n"
		"  ~SIMS           stop early once best move is decided (saves time / cpu). \n"
		"  =SIMS:MAX_SIMS  allow up-to MAX_SIMS simulations if best move is unclear. \n"
		"  SECS            fixed number of seconds per move \n"
		"                  Pachi will spend a little less to allow for network latency and other \n"
		"                  unexpected slowdowns. This is the same as one-period japanese byoyomi. \n"
		"  _SECS           absolute time: use fixed number of seconds for the whole game\n"
		" \n"
		"Engine args: \n"
		"  Comma/space separated engine specific options as in:\n"
		"      pachi threads=8 resign_threshold=0.25 pondering \n"
		"      pachi threads=8,resign_threshold=0.25,pondering            (pachi < 12.50) \n"
		"\n"
		"  See respective engines for details. Most common options for uct: \n"
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
#define OPT_KGS_CHAT	      272
#define OPT_SMART_PASS        273
#define OPT_EXT_JOSEKI_ENGINE 274
#define OPT_JOSEKIFIX         275
#define OPT_NOJOSEKIFIX       276


static struct option longopts[] = {
	{ "chatfile",               required_argument, 0, 'c' },
	{ "compile-flags",          no_argument,       0, OPT_COMPILE_FLAGS },
	{ "debug-level",            required_argument, 0, 'd' },
	{ "dcnn",                   optional_argument, 0, OPT_DCNN },
	{ "engine",                 required_argument, 0, 'e' },
#ifdef JOSEKIFIX
	{ "external-joseki-engine", required_argument, 0, OPT_EXT_JOSEKI_ENGINE },
#endif	
	{ "fbook",                  required_argument, 0, 'f' },
	{ "fuseki-time",            required_argument, 0, OPT_FUSEKI_TIME },
	{ "fuseki",                 required_argument, 0, OPT_FUSEKI },
#ifdef NETWORK
	{ "gtp-port",               required_argument, 0, 'g' },
	{ "log-port",               required_argument, 0, 'l' },
#endif
	{ "guess-unclear",          no_argument,       0, OPT_SMART_PASS },
	{ "help",                   no_argument,       0, 'h' },
	{ "joseki",                 no_argument,       0, OPT_JOSEKI },
#ifdef JOSEKIFIX
	{ "josekifix",              no_argument,       0, OPT_JOSEKIFIX },
#endif
	{ "kgs",                    no_argument,       0, OPT_KGS },
	{ "kgs-chat",               no_argument,       0, OPT_KGS_CHAT },
#ifdef DCNN
	{ "list-dcnns",             no_argument,       0, OPT_LIST_DCNNS },
#endif
	{ "log-file",               required_argument, 0, 'o' },
	{ "name",                   required_argument, 0, OPT_NAME },
	{ "nodcnn",                 no_argument,       0, OPT_NODCNN },
	{ "noundo",                 no_argument,       0, OPT_NOUNDO },
	{ "nojoseki",               no_argument,       0, OPT_NOJOSEKI },
#ifdef JOSEKIFIX
	{ "nojosekifix",            no_argument,       0, OPT_NOJOSEKIFIX },
#endif
	{ "nopassfirst",            no_argument,       0, OPT_NOPASSFIRST },
	{ "nopatterns",             no_argument,       0, OPT_NOPATTERNS },
	{ "patterns",               no_argument,       0, OPT_PATTERNS },
	{ "rules",                  required_argument, 0, 'r' },
	{ "seed",                   required_argument, 0, 's' },
	{ "smart-pass",             no_argument,       0, OPT_SMART_PASS },
	{ "time",                   required_argument, 0, 't' },
	{ "unit-test",              required_argument, 0, 'u' },
	{ "verbose-caffe",          no_argument,       0, OPT_VERBOSE_CAFFE },
	{ "version",                optional_argument, 0, 'v' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char *argv[])
{
	pachi_options_t *options = &main_options;

	gtp_t main_gtp;	
	gtp_t *gtp = &main_gtp;
	
	enum engine_id engine_id = E_UCT;
	time_info_t ti_default = ti_none;
	int  seed = time(NULL) ^ getpid();
	char *testfile = NULL;
	char *gtp_port = NULL;
	char *log_port = NULL;
	char *chatfile = NULL;
	char *fbookfile = NULL;
	FILE *file = NULL;
	bool verbose_caffe = false;

	pachi_init(argc, argv);
	
	int opt;
	int option_index;
	/* Leading ':' -> we handle error messages. */
	while ((opt = getopt_long(argc, argv, ":c:e:d:Df:g:hl:o:r:s:t:u:v::", longopts, &option_index)) != -1) {
		switch (opt) {
			case 'c':
				chatfile = strdup(optarg);
				break;
			case OPT_COMPILE_FLAGS:
				printf("Compiler:\n%s\n\n", PACHI_COMPILER);
				printf("CFLAGS:\n%s\n\n", PACHI_CFLAGS);
				printf("Command:\n%s\n", PACHI_CC1);
				exit(0);
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
			case 'e':
				engine_id = engine_name_to_id(optarg);
				if (engine_id == E_MAX)
					die("%s: invalid engine '%s'\n", argv[0], optarg);
				break;
#ifdef JOSEKIFIX
			case OPT_EXT_JOSEKI_ENGINE:
				external_joseki_engine_cmd = strdup(optarg);
				break;
#endif
			case 'f':
				fbookfile = strdup(optarg);
				break;
#ifdef NETWORK
			case 'g':
				gtp_port = strdup(optarg);
				break;
#endif
			case OPT_SMART_PASS:
				options->guess_unclear_groups = true;
				break;
			case 'h':
				usage(argv[optind]);
				exit(0);
			case OPT_JOSEKI:
				require_joseki();
				break;
#ifdef JOSEKIFIX
			case OPT_JOSEKIFIX:
				require_josekifix();
				break;
#endif
			case OPT_KGS:
				options->kgs = gtp->kgs = true;
				options->nopassfirst = true;           /* --nopassfirst */
				options->guess_unclear_groups = true;  /* only affects japanese games here */
				break;
			case OPT_KGS_CHAT:
				gtp->kgs_chat = true;
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
#ifdef JOSEKIFIX
			case OPT_NOJOSEKIFIX:
				disable_josekifix();
				break;
#endif
			case OPT_NOPASSFIRST:
				options->nopassfirst = true;
				break;
			case OPT_NOPATTERNS:
				disable_patterns();
				break;
			case OPT_PATTERNS:
				require_patterns();
				break;
			case 'r':
				options->forced_rules = board_parse_rules(optarg);
				if (options->forced_rules == RULES_INVALID)
					die("Unknown ruleset: %s\n", optarg);
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
				assert(ti_default.type != TT_NULL);
				break;
			case OPT_FUSEKI:
				set_fuseki_moves(atoi(optarg));
				break;
			case OPT_FUSEKI_TIME:
				if (!time_parse(&ti_fuseki, optarg))
					die("%s: Invalid --fuseki-time argument %s\n", argv[0], optarg);
				ti_fuseki.ignore_gtp = true;
				assert(ti_fuseki.type != TT_NULL);
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
	if (options->forced_rules) {
		b->rules = options->forced_rules;
		if (DEBUGL(1))  fprintf(stderr, "Rules: %s\n", rules2str(b->rules));
	}
	gtp_internal_init(gtp);
	gtp_init(gtp, b);

	time_info_t ti[S_MAX];
	ti[S_BLACK] = ti_default;
	ti[S_WHITE] = ti_default;

	chat_init(chatfile);
#ifdef JOSEKIFIX
	josekifix_init(b);
#endif

	/* Extra cmdline args are engine parameters */
	strbuf(buf, 1000);
	for (int i = optind; i < argc; i++)
		sbprintf(buf, "%s%s", (i == optind ? "" : ","), argv[i]);
	char *engine_args = buf->str;
	
	engine_t e;  engine_init(&e, engine_id, engine_args, b);
	network_init(gtp_port);

	while (1) {
		main_loop(gtp, b, &e, ti, &ti_default, gtp_port);
		if (!gtp_port)  break;
		network_init(gtp_port);
	}

	engine_done(&e);
	board_delete(&b);
	chat_done();
	free(testfile);
	free(gtp_port);
	free(log_port);
	free(chatfile);
	free(fbookfile);
	return 0;
}

static void
log_gtp_input(char *cmd)
{
#ifdef DISTRIBUTED
	/* Log everything except 'pachi-genmoves' subcommands by default,
	 * slave gets one every 100ms ... */
	bool genmoves_subcommand = (strchr(cmd, '@') && strstr(cmd, " pachi-genmoves"));
	if (genmoves_subcommand && !DEBUGL(3))  return;
#endif
	
	if (DEBUGL(1))  fprintf(stderr, "IN: %s", cmd);
}

static void
main_loop(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti, time_info_t *ti_default, char *gtp_port)
{
	char buf[4096];
	while (fgets(buf, 4096, stdin)) {
		log_gtp_input(buf);

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

#ifdef JOSEKIFIX
	if (external_joseki_engine) {
		engine_done(external_joseki_engine);
		external_joseki_engine = NULL;
	}
#endif
}
