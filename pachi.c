#define DEBUG
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <time.h>
#include <unistd.h>

#include "board.h"
#include "pachi.h"
#include "debug.h"
#include "engine.h"
#include "t-unit/test.h"
#include "gtp.h"
#include "chat.h"
#include "timeinfo.h"
#include "random.h"
#include "version.h"
#include "network.h"
#include "fifo.h"
#include "dcnn/dcnn.h"
#include "dcnn/caffe.h"
#include "pattern/pattern.h"
#include "pattern/spatial.h"
#include "pattern/prob.h"
#include "joseki/joseki.h"
#include "josekifix/joseki_override.h"
#include "josekifix/josekifix_engine.h"

/* Main options */
static pachi_options_t main_options = { 0, };
const  pachi_options_t *pachi_options() {  return &main_options;  }

char *pachi_exe = NULL;
char *pachi_dir = NULL;

int   debug_level = 3;
int   saved_debug_level;
bool  debug_boardprint = true;
long  verbose_logs = 0;

static void main_loop(gtp_t *gtp, board_t *b, engine_t *e, time_info_t *ti, time_info_t *ti_default, char *gtp_port);

static void
pachi_init(int argc, char *argv[])
{
	setlinebuf(stdout);
	setlinebuf(stderr);
	
	pachi_exe = argv[0];

	static char buf[512] = { 0, };
	strncpy(buf, pachi_exe, 511);
	pachi_dir = dirname(buf);

	win_set_pachi_cwd(argv[0]);

	engine_init_checks();
};

static engine_t *
new_main_engine(int engine_id, board_t *b, int argc, char **argv, int optind)
{
	/* Extra cmdline args are engine parameters */
	strbuf(buf, 1000);
	for (int i = optind; i < argc; i++)
		sbprintf(buf, "%s%s", (i == optind ? "" : ","), argv[i]);
	char *engine_args = buf->str;

	engine_t *e = new_engine(engine_id, engine_args, b);
	
#ifdef JOSEKIFIX
	/* When joseki fixes are active josekifix engine is main engine
	 * and acts as middle man between gtp and uct engine. */
	if (engine_id == E_UCT)
		e = josekifix_engine_if_needed(e, b);

	if (modern_joseki && !get_josekifix_enabled())
		die("Aborting: --modern-joseki needs josekifix module but currently unavailable/disabled.\n");
#endif
	return e;
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
			engine_reset(e, b);
		}
	}
}


/**********************************************************************************************************/
/* Options */

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
		"  -e, --engine ENGINE               select engine (default uct). Supported engines: \n");
	fprintf(stderr,
		"                                    %s \n", supported_engines(false));
	fprintf(stderr,
		"  -h, --help                        show usage \n"
		"  -v, --version                     show version \n"
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
		"      --banner BANNER               kgs game start message (default: \"Have a good game !\") \n"
		"                                    can use '+' instead of ' ' if you are wrestling with kgsGtp:"
		"                                      pachi --kgs --banner Have+a+good+game! \n"
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
		" \n"
		"Testing: \n"
                "      --compile-flags               show compiler flags \n"
		"  -s, --seed RANDOM_SEED            set random seed \n"
		"  -u, --unit-test FILE              run unit tests \n"
		"      --tunit-fatal                 abort on failed unit test \n"
		"      --gtp-fatal                   abort on gtp error \n"
		" \n"
		"Engine components: \n"
		"      --dcnn,     --nodcnn          dcnn required / disabled \n"
		"      --patterns, --nopatterns      mm patterns required / disabled \n"
		"                                    guides tree search.                (default: enabled) \n"
		"      --joseki,   --nojoseki        (nodcnn) joseki module required / disabled \n"
#ifdef JOSEKIFIX
		"      --josekifix, --nojosekifix    (dcnn)   josekifix module required / disabled \n"
		"                                    provides fixes for joseki lines that dcnn plays poorly, \n"
		"                                    and more modern josekis with --modern-joseki \n"
		"                                    (uses katago as as joseki engine)  (default: enabled) \n"
		"      --modern-joseki               play modern josekis:               (default: off) \n"
		"                                    katago handles first moves in each corner. \n"
		"      --external-joseki-engine CMD  use another joseki engine instead of katago. \n"
#endif
		" \n"
#ifdef DCNN
		"Deep learning: \n"
		"      --dcnn=name                   choose which dcnn to load (default detlef) \n"
		"      --dcnn=file                   \n"
		"      --list-dcnns                  show supported networks \n"
		"      --nodcnn-blunder              don't filter dcnn blunders         (default: enabled) \n"
		"      --verbose-caffe               enable caffe logging \n"		
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
#define OPT_NODCNN_BLUNDER    277
#define OPT_TUNIT_FATAL	      278
#define OPT_BANNER            279
#define OPT_GTP_FATAL         280
#define OPT_MODERN_JOSEKI     281


static struct option longopts[] = {
	{ "banner",                 required_argument, 0, OPT_BANNER },
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
	{ "gtp-fatal",		    no_argument,       0, OPT_GTP_FATAL },
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
#ifdef JOSEKIFIX
	{ "modern-joseki",          no_argument,       0, OPT_MODERN_JOSEKI },
#endif
	{ "name",                   required_argument, 0, OPT_NAME },
	{ "nodcnn",                 no_argument,       0, OPT_NODCNN },
	{ "nodcnn-blunder",         no_argument,       0, OPT_NODCNN_BLUNDER },
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
	{ "tunit-fatal",	    no_argument,       0, OPT_TUNIT_FATAL },
	{ "unit-test",              required_argument, 0, 'u' },
	{ "verbose-caffe",          no_argument,       0, OPT_VERBOSE_CAFFE },
	{ "version",                no_argument,       0, 'v' },
	{ 0, 0, 0, 0 }
};


/**********************************************************************************************************/
/* Main */

static gtp_t	 main_gtp;
static board_t	*main_board = NULL;
static engine_t *main_engine;

engine_t *
pachi_main_engine(void)
{
	return main_engine;
}

int main(int argc, char *argv[])
{
	pachi_options_t *options = &main_options;

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

	board_t *b = main_board = board_new(dcnn_default_board_size(), fbookfile);
	gtp_internal_init(gtp);
	gtp_init(gtp, b);
	gtp->banner = strdup("Have a good game !");
	
	int opt;
	int option_index;
	/* Leading ':' -> we handle error messages. */
	while ((opt = getopt_long(argc, argv, ":c:e:d:Df:g:hl:o:r:s:t:u:v::", longopts, &option_index)) != -1) {
		switch (opt) {
			case OPT_BANNER:
				if (gtp->banner)  free(gtp->banner);
				gtp->banner = strdup(optarg);
				/* Can use '+' instead of spaces. */
				for (char *s = gtp->banner; *s; s++)
					if (*s == '+') *s = ' ';
				break;
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
			case OPT_GTP_FATAL:
				gtp->fatal = true;
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
#ifdef JOSEKIFIX
			case OPT_MODERN_JOSEKI:
				modern_joseki = true;
				break;
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
			case OPT_NODCNN_BLUNDER:
				disable_dcnn_blunder();
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
			case OPT_TUNIT_FATAL:
				options->tunit_fatal = true;
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
	if (DEBUGL(0))           show_version(stderr);
	if (getenv("DATA_DIR"))
		if (DEBUGL(1))   fprintf(stderr, "Using data dir %s\n", getenv("DATA_DIR"));
	if (DEBUGL(2))	         fprintf(stderr, "Random seed: %d\n", seed);
	fifo_init();

	if (fbookfile) {
		b->fbookfile = strdup(fbookfile);
		board_clear(b);
	}
	if (options->forced_rules) {
		b->rules = options->forced_rules;
		if (DEBUGL(1))  fprintf(stderr, "Rules: %s\n", rules2str(b->rules));
	}

	time_info_t ti[S_MAX];
	ti[S_BLACK] = ti_default;
	ti[S_WHITE] = ti_default;

	chat_init(chatfile);

	if (testfile)		 return unit_test(testfile);

	engine_t *e = main_engine = new_main_engine(engine_id, b, argc, argv, optind);
	
	network_init(gtp_port);

	while (1) {
		main_loop(gtp, b, e, ti, &ti_default, gtp_port);
		if (!gtp_port)  break;
		network_init(gtp_port);
	}

	free(testfile);
	free(gtp_port);
	free(log_port);
	free(chatfile);
	free(fbookfile);
	
	pachi_done();
	
	return 0;
}

/* Also called on gtp quit command. */
void
pachi_done()
{
	delete_engine(&main_engine);
	board_delete(&main_board);
	gtp_done(&main_gtp);
	
	chat_done();
	joseki_done();
	prob_dict_done();
	spatial_dict_done();
}
