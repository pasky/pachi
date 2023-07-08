#define DEBUG
#include <assert.h>
#include <unistd.h>
#include <math.h>

/* Don't #include <openblas/cblas.h> just for this (build hell). */
void openblas_set_num_threads(int num_threads);

#include "debug.h"
#include "board.h"
#include "engine.h"
#include "caffe.h"
#include "dcnn.h"
#include "timeinfo.h"

typedef void (*dcnn_evaluate_t)(board_t *b, enum stone color, float result[]);
typedef bool (*dcnn_supported_board_size_t)(board_t *b);

typedef struct {
	char *name;
	char *full_name;
	char *model_filename;
	char *weights_filename;
	int  default_size;
	dcnn_supported_board_size_t supported_board_size;
	dcnn_evaluate_t             eval;
	int  *global_var;
} dcnn_t;


static bool board_19x19(board_t *b)        {  return (board_rsize(b) == 19);  }
static bool board_15x15(board_t *b)        {  return (board_rsize(b) == 15);  }
//static bool board_13x13(board_t *b)      {  return (board_rsize(b) == 13);  }
static bool board_13x13_and_up(board_t *b) {  return (board_rsize(b) >= 13);  }

#ifdef DCNN_DETLEF
static void detlef54_dcnn_eval(board_t *b, enum stone color, float result[]);
static void detlef44_dcnn_eval(board_t *b, enum stone color, float result[]);
#endif
#ifdef DCNN_DARKFOREST
static void darkforest_dcnn_eval(board_t *b, enum stone color, float result[]);
#endif

int darkforest_dcnn = 0;

static dcnn_t dcnns[] = {
#ifdef DCNN_DETLEF
{  "detlef",     "Detlef's 54%", "detlef54.prototxt",  "detlef54.trained", 19, board_13x13_and_up,   detlef54_dcnn_eval },
{  "detlef54",   "Detlef's 54%", "detlef54.prototxt",  "detlef54.trained", 19, board_13x13_and_up,   detlef54_dcnn_eval },
{  "detlef44",   "Detlef's 44%", "detlef44.prototxt",  "detlef44.trained", 19, board_19x19,          detlef44_dcnn_eval },
#endif
#ifdef DCNN_DARKFOREST
{  "df",         "Darkforest",   "df2.prototxt",       "df2.trained",      19, board_19x19,          darkforest_dcnn_eval,  &darkforest_dcnn },
{  "darkforest", "Darkforest",   "df2.prototxt",       "df2.trained",      19, board_19x19,          darkforest_dcnn_eval,  &darkforest_dcnn },
{  "df",         "Darkforest",   "df2_15x15.prototxt", "df2.trained",      15, board_15x15,          darkforest_dcnn_eval,  &darkforest_dcnn },
{  "darkforest", "Darkforest",   "df2_15x15.prototxt", "df2.trained",      15, board_15x15,          darkforest_dcnn_eval,  &darkforest_dcnn },
#endif
{  0, }
};

static dcnn_t *dcnn = NULL;

#define dcnn_supported_board_size(b) (dcnn->supported_board_size(b))

/* Find dcnn entry for @name (can also be model/weights filename). */
void
set_dcnn(char *name)
{
	for (int i = 0; dcnns[i].name; i++)
		if (!strcmp(name, dcnns[i].name) ||
		    !strcmp(name, dcnns[i].model_filename) ||
		    !strcmp(name, dcnns[i].weights_filename)) {
			dcnn = &dcnns[i];
			if (dcnn->global_var)
				*dcnn->global_var = 1;
			return;
		}
	
	die("Unknown dcnn '%s'\n", name);
}

void
list_dcnns()
{
	printf("Supported networks:\n");
	for (int i = 0; dcnns[i].name; i++)
		printf("  %-20s %s dcnn\n", dcnns[i].name, dcnns[i].full_name);
}

static int
find_dcnn_for_board(board_t *b)
{
	for (int i = 0; dcnns[i].name; i++)
		if (!strcmp(dcnn->name, dcnns[i].name) &&
		    dcnns[i].supported_board_size(b)) {  dcnn = &dcnns[i];  return 1;  }
	return 0;
}

int
dcnn_default_board_size()
{
	if (!dcnn)  dcnn = &dcnns[0];
	return dcnn->default_size;
}

static bool dcnn_enabled = true;
static bool dcnn_required = false;
void disable_dcnn(void)  {  dcnn_enabled = false;  }
void require_dcnn(void)  {  dcnn_required = true;  }

bool
using_dcnn(board_t *b)
{
	bool r = dcnn_enabled && dcnn_supported_board_size(b) && caffe_ready();
	if (dcnn_required && !r)  die("dcnn required but not used, aborting.\n");
	return r;
}

/* Set number of threads to use for dcnn evaluation (default: number of cores). */
void
dcnn_set_threads(int threads)
{
	if (!dcnn_enabled)
		return;
	
	openblas_set_num_threads(threads);
}

void
dcnn_init(board_t *b)
{
	if (!dcnn)  dcnn = &dcnns[0];
	if (dcnn_enabled && !dcnn_supported_board_size(b) && find_dcnn_for_board(b))
		caffe_done();  /* Reload net */	
	if (dcnn_enabled && dcnn_supported_board_size(b)) {
		caffe_init(board_rsize(b), dcnn->model_filename, dcnn->weights_filename, dcnn->full_name, dcnn->default_size);
		dcnn_blunder_init();
	}
	if (dcnn_required && !caffe_ready())  die("dcnn required, aborting.\n");
}

void
dcnn_evaluate_raw(board_t *b, enum stone color, float result[], ownermap_t *ownermap, bool debugl, char *extra_log)
{
	double time_start = time_now();
	dcnn->eval(b, color, result);
	
	if (debugl) {
		if (!extra_log)  extra_log = "";
		fprintf(stderr, "dcnn in %.2fs %s\n", time_now() - time_start, extra_log);
		
		coord_t best_c[DCNN_BEST_N];
		float   best_r[DCNN_BEST_N];
		get_dcnn_best_moves(b, result, best_c, best_r, DCNN_BEST_N);
		print_dcnn_best_moves(b, best_c, best_r, DCNN_BEST_N);
	}
}

void
dcnn_evaluate(board_t *b, enum stone color, float result[], ownermap_t *ownermap, bool debugl, char *extra_log)
{
	dcnn_evaluate_raw(b, color, result, ownermap, debugl, extra_log);
	dcnn_fix_blunders(b, color, result, ownermap, debugl);
}


#ifdef DCNN_DETLEF
/********************************************************************************************************/
/* Detlef's 54% dcnn */

/* 19 layers, 13 input planes:
 * http://computer-go.org/pipermail/computer-go/2015-December/008324.html
 * http://physik.de/CNNlast.tar.gz */

static void
detlef54_dcnn_eval(board_t *b, enum stone color, float result[])
{
	assert(dcnn_supported_board_size(b));

	int size = board_rsize(b);
	float data[13][size][size];
	memset(data, 0, sizeof(data));

	for (int x = 0; x < size; x++)
	for (int y = 0; y < size; y++) {
		coord_t c = coord_xy(x+1, y+1);
		group_t g = group_at(b, c);
		enum stone bc = board_at(b, c);
		int libs = board_group_info(b, g).libs - 1;
		if (libs > 3) libs = 3;
		
		if (bc == S_NONE)                    data[8][y][x] = 1.0;
		else if (bc == color)                data[0+libs][y][x] = 1.0;
		else if (bc == stone_other(color))   data[4+libs][y][x] = 1.0;
		
		if (c == last_move(b).coord)	     data[9][y][x] = 1.0;
		else if (c == last_move2(b).coord)   data[10][y][x] = 1.0;
		else if (c == last_move3(b).coord)   data[11][y][x] = 1.0;
		else if (c == last_move4(b).coord)   data[12][y][x] = 1.0;
	}

	caffe_get_data((float*)data, result, size, 13, size);
}


/********************************************************************************************************/
/* Detlef's 44% dcnn */

/* 19 layers, 2 input planes:
 * http://computer-go.org/pipermail/computer-go/2015-April/007573.html
 * http://physik.de/net.tgz */

static void
detlef44_dcnn_eval(board_t *b, enum stone color, float result[])
{
	enum stone other_color = stone_other(color);

	int size = board_rsize(b);
	float data[2][size][size];
	memset(data, 0, sizeof(data));

	for (int y = 0; y < size; y++)
	for (int x = 0; x < size; x++) {
                coord_t c = coord_xy(x+1, y+1);
		if (board_at(b, c) == color)        data[0][y][x] = 1;
		if (board_at(b, c) == other_color)  data[1][y][x] = 1;			
	}

	caffe_get_data((float*)data, result, size, 2, size);
}
#endif /* DCNN_DETLEF */


#ifdef DCNN_DARKFOREST
/********************************************************************************************************/
/* Darkforest dcnn */

/* 12 layers, 25 input planes, trained to predict next 3 moves:
 * https://github.com/facebookresearch/darkforestGo
 * https://arxiv.org/abs/1511.06410 */

static void
df_distance_transform(float *arr, int size)
{
	// First dimension.
	for (int j = 0; j < size; j++) {
		for (int i = 1; i < size; i++) 
			arr[i*size + j] = MIN(arr[i*size + j], arr[(i-1)*size + j] + 1);
		for (int i = size - 2; i >= 0; i--) 
			arr[i*size + j] = MIN(arr[i*size + j], arr[(i+1)*size + j] + 1);
	}
	// Second dimension
	for (int i = 0; i < size; i++) {
		for (int j = 1; j < size; j++)
			arr[i*size + j] = MIN(arr[i*size + j], arr[i*size + (j-1)] + 1);
		for (int j = size - 2; j >= 0; j--) 
			arr[i*size + j] = MIN(arr[i*size+j], arr[i * size + (j+1)] + 1);
	}
}

static void
df_get_distance_map(board_t *b, enum stone color, float *data) 
{
	int size = board_rsize(b);
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < size; j++) {
			coord_t c = coord_xy(i+1, j+1);
			if (board_at(b, c) == color)
				data[i*size + j] = 0;
			else
				data[i*size + j] = 10000;
		}
	}
	df_distance_transform(data, size);
}

static float
df_board_history_decay(board_t *b, coord_t coord, enum stone color)
{
	int v = 0;
	if (board_at(b, coord) == color || board_at(b, coord) == S_NONE)
		v = b->moveno[coord];
	return exp(0.1 * (v - (b->moves+1)));
}

static void
darkforest_dcnn_eval(board_t *b, enum stone color, float result[])
{
	enum stone other_color = stone_other(color);
	int size = board_rsize(b);
	float data[25][size][size];
	memset(data, 0, sizeof(data));
	
	float our_dist[size * size];
	float opponent_dist[size * size];
	df_get_distance_map(b, color, our_dist);
	df_get_distance_map(b, other_color, opponent_dist);
	
	for (int y = 0; y < size; y++)
	for (int x = 0; x < size; x++) {
		int p = size * y + x;
		coord_t c = coord_xy(x+1, y+1);
		group_t g = group_at(b, c);
		enum stone bc = board_at(b, c);
		int libs = board_group_info(b, g).libs;
		if (libs > 3) libs = 3;
			
		/* plane 0: our stones with 1 liberty */
		/* plane 1: our stones with 2 liberties */
		/* plane 2: our stones with 3+ liberties */
		if (bc == color)              data[libs-1][y][x] = 1;

		/* planes 3, 4, 5: opponent liberties */
		if (bc == other_color)        data[3+libs-1][y][x] = 1;

		/* plane 6: our simple ko  (but actually, our stones. typo ?) */
		if (bc == color)              data[6][y][x] = 1;

		/* plane 7: our stones. */
		if (bc == color)              data[7][y][x] = 1;

		/* plane 8: opponent stones. */
		if (bc == other_color)        data[8][y][x] = 1;

		/* plane 9: empty spots. */
		if (bc == S_NONE)             data[9][y][x] = 1;

		/* plane 10: our history */
		/* FIXME -1 for komi */
		data[10][y][x] = df_board_history_decay(b, c, color);
			
		/* plane 11: opponent history */
		/* FIXME -1 for komi */
		data[11][y][x] = df_board_history_decay(b, c, other_color);

		/* plane 12: border */
		if (!x || !y || x == size-1 || y == size-1)
			data[12][y][x] = 1.0;
		
		/* plane 13: position mask - distance from corner */
		float m = (float)(size+1) / 2;
		data[13][y][x] = expf(-0.5 * ((x-m)*(x-m) + (y-m)*(y-m)));

		/* plane 14: closest color is ours */
		data[14][y][x] = (our_dist[p] < opponent_dist[p]);

		/* plane 15: closest color is opponent */
		data[15][y][x] = (opponent_dist[p] < our_dist[p]);

		/* planes 16-24: encode rank - set 9th plane for 9d */
		data[24][y][x] = 1.0;
	}

	caffe_get_data((float*)data, result, size, 25, size);
}
#endif /* DCNN_DARKFOREST */


/********************************************************************************************************/

void
get_dcnn_best_moves(board_t *b, float *r, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++) {
		best_c[i] = pass;  best_r[i] = 0;
	}

	foreach_free_point(b) {
		int k = coord2dcnn_idx(c);
		best_moves_add(c, r[k], best_c, best_r, nbest);
	} foreach_free_point_end;
}

void
print_dcnn_best_moves(board_t *b, coord_t *best_c, float *best_r, int nbest)
{
	int cols = best_moves_print(b, "dcnn = ", best_c, nbest);

	fprintf(stderr, "%*s[ ", cols, "");
	for (int i = 0; i < nbest; i++)
		fprintf(stderr, "%-3i ", (int)(best_r[i] * 100));
	fprintf(stderr, "]\n");
}

