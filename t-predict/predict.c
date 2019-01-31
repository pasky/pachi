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
collect_move_stats(struct board *b, struct move *m, coord_t *best_c, int *guessed_move, int *total_move)
{
	int i = MIN(b->moves/10, PREDICT_MOVE_MAX/10 - 1);
	if (m->coord == best_c[0])
		guessed_move[i]++;
	total_move[i]++;
}

static void
collect_prob_stats(struct move *m, coord_t *best_c, float *best_r, int *guessed_by_prob, int *total_by_prob)
{
	for (int k = 0; k < PREDICT_TOPN; k++) {
		int i = best_r[k] * 10;
		assert(i >= 0); assert(i < PREDICT_PROBS);

		total_by_prob[i]++;
		if (m->coord == best_c[k])
			guessed_by_prob[i]++;
	}
}

static void
collect_topn_stats(struct move *m, coord_t *best_c, int *guessed_top)
{
	int k; /* Correct move is kth guess */
	for (k = 0; k < PREDICT_TOPN; k++)
		if (m->coord == best_c[k])
			break;
	for (int i = k; i < PREDICT_TOPN; i++)
		guessed_top[i]++;	
}

static void
collect_avg_val(int i, float val, float prob_max, float *probs_sum, float *devs_sum, int total)
{
	if (!(0 <= val && val <= prob_max)) {
		fprintf(stderr, "predict: prob for top%i not in [0.0 - %.1f]: %.2f\n", 
			i+1, prob_max, val);
		assert(0);
	}
	probs_sum[i] += val;
	float avg = probs_sum[i] / total;
	devs_sum[i] += DEVIATION_TERM(val, avg);
}

static void
collect_avg_stats(float *best_r, float *probs_sum, float *devs_sum, int total)
{
	for (int i = 0; i < PREDICT_TOPN; i++)
		collect_avg_val(i, best_r[i], PROB_MAX, probs_sum, devs_sum, total);
}

#define RESCALE_LOG(p)  (log(1 + p * 1000))

static void
collect_avg_log_stats(float *best_r, float *probs_sum, float *devs_sum, int total)
{
	for (int i = 0; i < PREDICT_TOPN; i++)
		collect_avg_val(i, RESCALE_LOG(best_r[i]), RESCALE_LOG(PROB_MAX), probs_sum, devs_sum, total);
}


static void
print_predict_move_stats(strbuf_t *buf, int *guessed_move, int *total_move)
{
	sbprintf(buf, "Predictions by move number:\n");
	for (int i = 0; i < PREDICT_MOVE_MAX/10; i++) {
		int pc = (total_move[i] ? round(guessed_move[i] * 100 / total_move[i]) : 0);
		sbprintf(buf, "  move %3i-%-3i: %4i/%-4i (%2i%%) %.*s\n",
			 i*10, (i+1)*10-1, guessed_move[i], total_move[i], pc, pc, stars);
	}
	sbprintf(buf, " \n");	
}

static void
print_predict_move_stats_short(strbuf_t *buf, int *guessed_move, int *total_move)
{
	int step = 3;
	sbprintf(buf, "Predictions by move number: (short)\n");
	for (int k = 0; k < PREDICT_MOVE_MAX/10 / step; k++) {
		int guessed = 0, total = 0;
		for (int j = 0; j < step; j++) {
			int i = k * step + j;
			guessed += guessed_move[i];
			total += total_move[i];
		}
		int pc = (total ? round(guessed * 100 / total) : 0);
		sbprintf(buf, "  move %3i-%-3i: %4i/%-4i (%2i%%) %.*s\n",
			 k*step*10, ((k+1)*step)*10-1, guessed, total, pc, pc, stars);
	}
	sbprintf(buf, " \n");	
}

static void
print_avg_stats(strbuf_t *buf, char *title, int scale,
		float prob_max, float *probs_sum, float *devs_sum, int total)
{
	sbprintf(buf, "%s\n", title);
	for (int i = 0; i < PREDICT_TOPN; i++) {
		float avg = probs_sum[i] / total;
		float dev = DEVIATION(devs_sum[i], total);
		char diag[scale];
		avg_dev_diagram(diag, sizeof(diag), avg, dev, prob_max);
		
		sbprintf(buf, "  #%-2i: %.2f ±%.2f  %s\n", i+1, avg, dev, diag);
		if (avg < 0.01 * prob_max)
			break;
	}
	sbprintf(buf, " \n");
}

static void
print_topn_stats(strbuf_t *buf, int *guessed_top, int total, int games)
{
	#define PREDICT_SCALE  3/4
	sbprintf(buf, "Topn stats: (Games: %i)\n", games);
	int pc = round(guessed_top[0] * 100 / total);
	sbprintf(buf, "Predicted   : %5i/%-5i moves (%2i%%)  %.*s\n",
		guessed_top[0], total, pc, pc * PREDICT_SCALE, stars);
	for (int i = 1; i < PREDICT_TOPN; i++) {
		pc = round(guessed_top[i] * 100 / total);
		sbprintf(buf, "  in best %2i: %5i/%-5i moves (%2i%%)  %.*s\n",
			 i+1, guessed_top[i], total, pc, pc * PREDICT_SCALE, stars);
	}
}

static void
print_prob_stats(strbuf_t *buf, int *guessed_by_prob, int *total_by_prob)
{
	sbprintf(buf, "Hits by probability vs expected value:\n");
	for (int i = 9; i >= 0; i--) {
		int expected = i * 10 + 5;
		sbprintf(buf, "  [%2i%% - %3i%%]: ", expected - 5, expected + 5);
		if (!total_by_prob[i]) {  sbprintf(buf, "NA\n"); continue;  }

		char diag[62] = "                                          ";
		int pc = round(guessed_by_prob[i] * 100 / total_by_prob[i]);
		int start = expected - 30;  // can be negative
		int end   = MIN(expected + 30, 100);
		for (int j = start; j < end; j++) {
			if (pc < expected && j >= pc && j <= expected)  diag[j - start] = '*';
			if (expected < pc && j >= expected && j <= pc)  diag[j - start] = '*';
		}
		diag[expected - start] = '|';

		sbprintf(buf, "%5i/%-6i (%2i%%)   %+3i%%  %s\n",
			 guessed_by_prob[i], total_by_prob[i], pc, pc - expected, diag);
	}
	sbprintf(buf, " \n");
}


static char *
predict_stats(struct board *b, struct move *m, coord_t *best_c, float *best_r, int games)
{
	static int total_ = 0;
	int total = ++total_;

	/* Stats by move number */
	static int guessed_move[PREDICT_MOVE_MAX/10] = {0, };
	static int total_move[PREDICT_MOVE_MAX/10] = {0, };
	collect_move_stats(b, m, best_c, guessed_move, total_move);

	/* Average values */
	/* Assumes properly scaled probs in [0.0 - PROB_MAX] */
	static float probs_sum[PREDICT_TOPN] = {0, };
	static float devs_sum[PREDICT_TOPN] = {0, };
	collect_avg_stats(best_r, probs_sum, devs_sum, total);

	/* Average log values */
	static float log_probs_sum[PREDICT_TOPN] = {0, };
	static float log_devs_sum[PREDICT_TOPN] = {0, };
	collect_avg_log_stats(best_r, log_probs_sum, log_devs_sum, total);

	/* Check Probabilities */

	static int guessed_by_prob[PREDICT_PROBS] = {0, };
	static int total_by_prob[PREDICT_PROBS] = {0, };
	collect_prob_stats(m, best_c, best_r, guessed_by_prob, total_by_prob);

	/* Topn stats */
	static int guessed_top[PREDICT_TOPN] = {0, };
	collect_topn_stats(m, best_c, guessed_top);


	/* Dump stats from time to time */
	if (total % 200 == 0) {
		strbuf_t strbuf;
		strbuf_t *buf = strbuf_init_alloc(&strbuf, 16384);

		sbprintf(buf, " \n");
		print_predict_move_stats(buf, guessed_move, total_move);
		print_predict_move_stats_short(buf, guessed_move, total_move);
		print_prob_stats(buf, guessed_by_prob, total_by_prob);
		print_avg_stats(buf, "Average log values:", 50, RESCALE_LOG(PROB_MAX), log_probs_sum, log_devs_sum, total);
		print_avg_stats(buf, "Average values:",     50, PROB_MAX,              probs_sum,     devs_sum,     total);
		print_topn_stats(buf, guessed_top, total, games);
		
		return buf->str;
	}
	return NULL;
}

char *
predict_move(struct board *b, struct engine *e, struct time_info *ti, struct move *m, int games)
{
	enum stone color = m->color;
	
	if (m->coord == pass || m->coord == resign) {
		int r = board_play(b, m);  assert(r >= 0);
		return NULL;
	}

	if (DEBUGL(5))
		fprintf(stderr, "predict move %d,%d,%d\n", m->color, coord_x(m->coord, b), coord_y(m->coord, b));
	if (DEBUGL(1) && debug_boardprint)
		engine_board_print(e, b, stderr);

	// Not bothering with timer here for now.

	float   best_r[PREDICT_TOPN];
	coord_t best_c[PREDICT_TOPN];
	for (int i = 0; i < PREDICT_TOPN; i++)
		best_c[i] = pass;
	struct time_info *ti_genmove = time_info_genmove(b, ti, color);
	engine_best_moves(e, b, ti_genmove, color, best_c, best_r, PREDICT_TOPN);
	//print_dcnn_best_moves(b, best_c, best_r, PREDICT_TOPN);

	// Play correct expected move
	if (board_play(b, m) < 0)
		die("ILLEGAL EXPECTED MOVE: [%s, %s]\n", coord2sstr(m->coord, b), stone2str(m->color));

	fprintf(stderr, "WINNER is %s in the actual game.\n", coord2sstr(m->coord, b));		
	if (best_c[0] == m->coord)
		fprintf(stderr, "Move %3i: Predict: Correctly predicted %s %s\n", b->moves,
			(color == S_BLACK ? "b" : "w"), coord2sstr(best_c[0], b));
	else
		fprintf(stderr, "Move %3i: Predict: Wrong prediction: %s %s != %s\n", b->moves,
			(color == S_BLACK ? "b" : "w"), coord2sstr(best_c[0], b), coord2sstr(m->coord, b));

	if (DEBUGL(1) && debug_boardprint)
		engine_board_print(e, b, stderr);

	// Record/show stats
	return predict_stats(b, m, best_c, best_r, games);
}
