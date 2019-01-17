#define DEBUG
#include <assert.h>
#include <unistd.h>

#include "debug.h"
#include "board.h"
#include "engine.h"
#include "uct/tree.h"
#include "caffe.h"
#include "dcnn.h"
#include "timeinfo.h"

static bool board_19x19(board_t *b)        {  return (board_rsize(b) == 19);  }
static bool board_15x15(board_t *b)        {  return (board_rsize(b) == 15);  }
//static bool board_13x13(board_t *b)      {  return (board_rsize(b) == 13);  }
static bool board_13x13_and_up(board_t *b) {  return (board_rsize(b) >= 13);  }

#ifdef DCNN_DETLEF
static void detlef54_dcnn_eval(board_t *b, enum stone color, float result[]);
static void detlef44_dcnn_eval(board_t *b, enum stone color, float result[]);
#endif

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
} dcnn_t;

static dcnn_t dcnns[] = {
#ifdef DCNN_DETLEF
{  "detlef",     "Detlef's 54%", "detlef54.prototxt",  "detlef54.trained", 19, board_13x13_and_up,   detlef54_dcnn_eval },
{  "detlef54",   "Detlef's 54%", "detlef54.prototxt",  "detlef54.trained", 19, board_13x13_and_up,   detlef54_dcnn_eval },
{  "detlef44",   "Detlef's 44%", "detlef44.prototxt",  "detlef44.trained", 19, board_19x19,          detlef44_dcnn_eval },
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

void
dcnn_init(board_t *b)
{
	if (!dcnn)  dcnn = &dcnns[0];
	if (dcnn_enabled && !dcnn_supported_board_size(b) && find_dcnn_for_board(b))
		caffe_done();  /* Reload net */	
	if (dcnn_enabled && dcnn_supported_board_size(b))
		caffe_init(board_rsize(b), dcnn->model_filename, dcnn->weights_filename, dcnn->full_name, dcnn->default_size);
	if (dcnn_required && !caffe_ready())  die("dcnn required, aborting.\n");
}

void
dcnn_evaluate_quiet(board_t *b, enum stone color, float result[])
{
	dcnn->eval(b, color, result);
}

void
dcnn_evaluate(board_t *b, enum stone color, float result[])
{
	double time_start = time_now();	
	dcnn->eval(b, color, result);
	if (DEBUGL(2))  fprintf(stderr, "dcnn in %.2fs\n", time_now() - time_start);	
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

/********************************************************************************************************/
#endif /* DCNN_DETLEF */


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

