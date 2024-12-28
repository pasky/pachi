#define DEBUG
#include <assert.h>
#include <math.h>
#include "board.h"
#include "debug.h"
#include "timeinfo.h"
#include "engine.h"
#include "t-predict/predict.h"

/* Standard deviation or Mean absolute deviation ? */
#define USE_STD_DEVIATION 1
//#define USE_MEAN_ABS_DEVIATION 1

#ifdef USE_STD_DEVIATION
  #define DEVIATION_TERM(val, avg)      ( (val - avg) * (val - avg) )
  #define DEVIATION(sq_devs_sum, total) ( sqrt(sq_devs_sum / total) )
#else /* Mean absolute deviation */
  #define DEVIATION_TERM(val, avg)      ( fabs(val - avg) )
  #define DEVIATION(devs_sum, total)    ( devs_sum / total )
#endif

#define PREDICT_TOPN 20
#define PREDICT_MOVE_MAX 320
#define PROB_MAX 1.0
#define PREDICT_PROBS 11   // PROB_MAX * 10 + 1


/**************************************************************************************************************/
/* Collect stats */

typedef struct {
	int guessed;
	int moves;
} guess_stats_t;

typedef struct {
	float probs_sum;
	float devs_sum;
} avg_stats_t;

typedef struct {
	guess_stats_t		by_move_number[PREDICT_MOVE_MAX/10];
	guess_stats_t		by_prob[PREDICT_PROBS];
	int			guessed_top[PREDICT_TOPN];
	avg_stats_t		avg_stats[PREDICT_TOPN];
	avg_stats_t		avg_log_stats[PREDICT_TOPN];
} predict_stats_t;


/* Stats by move number */
static void
collect_move_stats(predict_stats_t *stats, board_t *b, bool guessed)
{
	int i = MIN(b->moves/10, PREDICT_MOVE_MAX/10 - 1);
	if (guessed)
		stats->by_move_number[i].guessed++;
	stats->by_move_number[i].moves++;
}

/* Check Probabilities */
static void
collect_prob_stats(predict_stats_t *stats, move_t *m, coord_t *best_c, float *best_r)
{
	for (int k = 0; k < PREDICT_TOPN; k++) {
		int i = best_r[k] * 10;
		assert(i >= 0); assert(i < PREDICT_PROBS);

		if (best_c[k] == m->coord)
			stats->by_prob[i].guessed++;
		stats->by_prob[i].moves++;
	}
}

/* Topn stats */
static void
collect_topn_stats(predict_stats_t *stats, move_t *m, coord_t *best_c)
{
	int k; /* Correct move is kth guess */
	for (k = 0; k < PREDICT_TOPN; k++)
		if (best_c[k] == m->coord)
			break;
	for (int i = k; i < PREDICT_TOPN; i++)
		stats->guessed_top[i]++;
}

/* Assumes properly scaled probs in [0.0 - PROB_MAX] */
static void
collect_avg_val(int i, float val, float prob_max, avg_stats_t *avg_stats, int total)
{
	if (!(0 <= val && val <= prob_max)) {
		fprintf(stderr, "predict: prob for top%i not in [0.0 - %.1f]: %.2f\n", 
			i+1, prob_max, val);
		assert(0);
	}
	avg_stats[i].probs_sum += val;
	float avg = avg_stats[i].probs_sum / total;
	avg_stats[i].devs_sum += DEVIATION_TERM(val, avg);
}

/* Average values */
static void
collect_avg_stats(predict_stats_t *stats, float *best_r, int moves)
{
	for (int i = 0; i < PREDICT_TOPN; i++)
		collect_avg_val(i, best_r[i], PROB_MAX, stats->avg_stats, moves);
}

/* Average log values */
#define RESCALE_LOG(p)  (log(1 + p * 1000))

static void
collect_avg_log_stats(predict_stats_t *stats, float *best_r, int moves)
{
	for (int i = 0; i < PREDICT_TOPN; i++)
		collect_avg_val(i, RESCALE_LOG(best_r[i]), RESCALE_LOG(PROB_MAX), stats->avg_log_stats, moves);
}

static void
collect_stats(predict_stats_t *stats, board_t *b, move_t *m, coord_t *best_c, float *best_r, int moves, int games)
{
	bool guessed = (best_c[0] == m->coord);
	
	/* Stats by move number */
	collect_move_stats(stats, b, guessed);

	/* Average values */
	collect_avg_stats(stats, best_r, moves);

	/* Average log values */
	collect_avg_log_stats(stats, best_r, moves);

	/* Check Probabilities */
	collect_prob_stats(stats, m, best_c, best_r);

	/* Topn stats */
	collect_topn_stats(stats, m, best_c);
}


/**************************************************************************************************************/
/* Print stats */


static char *stars = "****************************************************************************************************";

/* Make average + deviation diagram */
static void
avg_dev_diagram(char *diag, int len, float avg, float dev, float prob_max)
{
	int scale = len - 1;
	assert(avg >= 0); assert(dev >= 0);
	int avg_pc = round(avg * scale / prob_max);

	memset(diag, ' ', scale);
	memset(diag, '*', avg_pc);
	diag[len - 1] = 0;
	
	/* Deviation */
	int dev_pc = round(dev * scale / prob_max);
	if (dev_pc >= 2) {  /* Not too small ?*/
		int lower = round((avg - dev) * scale / prob_max);
		int upper = round((avg + dev) * scale / prob_max);
		
		/* If we hit boundary on one side, shift the other */
		if (lower < 0)      {  upper += -lower;             lower = 0;        }
		if (upper >= scale) {  lower -= upper - (scale-1);  upper = scale-1;  }
		
		if (0 <= lower && lower < scale &&
		    0 <= upper && upper < scale) {  /* Sane ? */
			diag[lower] = '[';  diag[upper] = ']';
		}
	}	
}

static void
print_by_move_number_stats(predict_stats_t *stats, strbuf_t *buf)
{
	sbprintf(buf, "Predictions by move number:\n");
	for (int i = 0; i < PREDICT_MOVE_MAX/10; i++) {
		int guessed = stats->by_move_number[i].guessed;
		int moves = stats->by_move_number[i].moves;
		int pc = (moves ? round(guessed * 100 / moves) : 0);
		sbprintf(buf, "  move %3i-%-3i: %4i/%-4i (%2i%%) %.*s\n",
			 i*10, (i+1)*10-1, guessed, moves, pc, pc, stars);
	}
	sbprintf(buf, " \n");	
}

static void
print_by_move_number_stats_short(predict_stats_t *stats, strbuf_t *buf)
{
	int step = 3;
	sbprintf(buf, "Predictions by move number: (short)\n");
	for (int k = 0; k < PREDICT_MOVE_MAX/10 / step; k++) {
		int guessed = 0, moves = 0;
		for (int j = 0; j < step; j++) {
			int i = k * step + j;
			guessed += stats->by_move_number[i].guessed;
			moves += stats->by_move_number[i].moves;
		}
		int pc = (moves ? round(guessed * 100 / moves) : 0);
		sbprintf(buf, "  move %3i-%-3i: %4i/%-4i (%2i%%) %.*s\n",
			 k*step*10, ((k+1)*step)*10-1, guessed, moves, pc, pc, stars);
	}
	sbprintf(buf, " \n");	
}

static void
print_avg_stats(strbuf_t *buf, char *title, int scale,
		float prob_max, avg_stats_t *avg_stats, int total)
{
	sbprintf(buf, "%s\n", title);
	for (int i = 0; i < PREDICT_TOPN; i++) {
		float avg = avg_stats[i].probs_sum / total;
		float dev = DEVIATION(avg_stats[i].devs_sum, total);
		char diag[scale];
		avg_dev_diagram(diag, sizeof(diag), avg, dev, prob_max);
		
		sbprintf(buf, "  #%-2i: %.2f ±%.2f  %s\n", i+1, avg, dev, diag);
		if (avg < 0.01 * prob_max)
			break;
	}
	sbprintf(buf, " \n");
}

static void
print_topn_stats(predict_stats_t *stats, strbuf_t *buf, int moves, int games)
{
	#define PREDICT_SCALE  3/4
	sbprintf(buf, "Topn stats: (Games: %i)\n", games);
	int guessed = stats->guessed_top[0];
	int pc = round(guessed * 100 / moves);
	sbprintf(buf, "Predicted   : %5i/%-5i moves (%2i%%)  %.*s\n",
		guessed, moves, pc, pc * PREDICT_SCALE, stars);
	for (int i = 1; i < PREDICT_TOPN; i++) {
		guessed = stats->guessed_top[i];
		pc = round(guessed * 100 / moves);
		sbprintf(buf, "  in best %2i: %5i/%-5i moves (%2i%%)  %.*s\n",
			 i+1, guessed, moves, pc, pc * PREDICT_SCALE, stars);
	}
}

static void
print_prob_stats(predict_stats_t *stats, strbuf_t *buf)
{
	sbprintf(buf, "Hits by probability vs expected value:\n");
	for (int i = 9; i >= 0; i--) {
		int guessed = stats->by_prob[i].guessed;
		int moves = stats->by_prob[i].moves;
		int expected = i * 10 + 5;
		sbprintf(buf, "  [%2i%% - %3i%%]: ", expected - 5, expected + 5);
		if (!moves) {  sbprintf(buf, "NA\n"); continue;  }

		char diag[62] = "                                          ";
		int pc = round(guessed * 100 / moves);
		int start = expected - 30;  // can be negative
		int end   = MIN(expected + 30, 100);
		for (int j = start; j < end; j++) {
			if (pc < expected && j >= pc && j <= expected)  diag[j - start] = '*';
			if (expected < pc && j >= expected && j <= pc)  diag[j - start] = '*';
		}
		diag[expected - start] = '|';

		sbprintf(buf, "%5i/%-6i (%2i%%)   %+3i%%  %s\n",
			 guessed, moves, pc, pc - expected, diag);
	}
	sbprintf(buf, " \n");
}

static char *
print_stats(predict_stats_t *stats, int moves, int games)
{
	static_strbuf(buf, 16384);
	
	sbprintf(buf, " \n");
	print_by_move_number_stats(stats, buf);
	print_by_move_number_stats_short(stats, buf);
	print_prob_stats(stats, buf);
	print_avg_stats(buf, "Average log values:", 50, RESCALE_LOG(PROB_MAX), stats->avg_log_stats, moves);
	print_avg_stats(buf, "Average values:",     50, PROB_MAX,              stats->avg_stats,     moves);
	print_topn_stats(stats, buf, moves, games);
	
	return buf->str;
}


/**************************************************************************************************************/

char *
predict_move(board_t *b, engine_t *e, time_info_t *ti, move_t *m, int games)
{
	enum stone color = m->color;
	
	if (m->coord == pass || m->coord == resign) {
		int r = board_play(b, m);  assert(r >= 0);
		return NULL;
	}

	if (DEBUGL(5))  fprintf(stderr, "predict move %d,%d,%d\n", m->color, coord_x(m->coord), coord_y(m->coord));
	if (DEBUGL(1) && debug_boardprint)  engine_board_print(e, b, stderr);

	// Not bothering with timer here for now.

	float   best_r[PREDICT_TOPN];
	coord_t best_c[PREDICT_TOPN];
	for (int i = 0; i < PREDICT_TOPN; i++)
		best_c[i] = pass;
	time_info_t *ti_genmove = time_info_genmove(b, ti, color);
	engine_best_moves(e, b, ti_genmove, color, best_c, best_r, PREDICT_TOPN);
	//print_dcnn_best_moves(b, best_c, best_r, PREDICT_TOPN);

	// Play correct expected move
	if (board_play(b, m) < 0)
		die("ILLEGAL EXPECTED MOVE: [%s, %s]\n", coord2sstr(m->coord), stone2str(m->color));

	fprintf(stderr, "WINNER is %s in the actual game.\n", coord2sstr(m->coord));
	if (best_c[0] == m->coord)
		fprintf(stderr, "Move %3i: Predict: Correctly predicted %s %s\n", b->moves,
			(color == S_BLACK ? "b" : "w"), coord2sstr(best_c[0]));
	else
		fprintf(stderr, "Move %3i: Predict: Wrong prediction: %s %s != %s\n", b->moves,
			(color == S_BLACK ? "b" : "w"), coord2sstr(best_c[0]), coord2sstr(m->coord));

	if (DEBUGL(1) && debug_boardprint)
		engine_board_print(e, b, stderr);

	// Collect stats
	static int moves = 0;
	static predict_stats_t stats = { 0, };
	collect_stats(&stats, b, m, best_c, best_r, ++moves, games);
	
	// Dump stats from time to time
	if (moves % 200 == 0)
		return print_stats(&stats, moves, games);
	
	return NULL;
}
