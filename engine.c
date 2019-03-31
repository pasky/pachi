#define DEBUG
#include "engine.h"
#include "pachi.h"


void
engine_init(engine_t *e, int id, char *e_arg, board_t *b)
{
	pachi_engine_init(e, id, e_arg, b);
}

void
engine_done(engine_t *e)
{
	if (e->done) e->done(e);
	if (e->data) free(e->data);
}

engine_t*
new_engine(int id, char *e_arg, board_t *b)
{
	engine_t *e = malloc2(engine_t);
	engine_init(e, id, e_arg, b);
	return e;
}

void
delete_engine(engine_t **e)
{
	engine_done(*e);
	free(*e);
	*e = NULL;
}

void
engine_reset(engine_t *e, board_t *b, char *e_arg)
{
	int engine_id = e->id;
	b->es = NULL;
	engine_done(e);
	engine_init(e, engine_id, e_arg, b);
}

void
engine_board_print(engine_t *e, board_t *b, FILE *f)
{
	(e->board_print ? e->board_print(e, b, f) : board_print(b, f));
}

void
engine_best_moves(engine_t *e, board_t *b, time_info_t *ti, enum stone color, 
		  coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++) {
		best_c[i] = pass;  best_r[i] = 0;
	}
	e->best_moves(e, b, ti, color, best_c, best_r, nbest);
}

ownermap_t*
engine_ownermap(engine_t *e, board_t *b)
{
	return (e->ownermap ? e->ownermap(e, b) : NULL);
}

/* For engines best_move(): Add move @c with prob @r to best moves @best_c, @best_r */
void
best_moves_add(coord_t c, float r, coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++)
		if (r > best_r[i]) {
			for (int j = nbest - 1; j > i; j--) { // shift
				best_r[j] = best_r[j - 1];
				best_c[j] = best_c[j - 1];
			}
			best_r[i] = r;
			best_c[i] = c;
			break;
		}
}

void
best_moves_add_full(coord_t c, float r, void *d, coord_t *best_c, float *best_r, void **best_d, int nbest)
{
	for (int i = 0; i < nbest; i++)
		if (r > best_r[i]) {
			for (int j = nbest - 1; j > i; j--) { // shift
				best_r[j] = best_r[j - 1];
				best_c[j] = best_c[j - 1];
				best_d[j] = best_d[j - 1];
			}
			best_r[i] = r;
			best_c[i] = c;
			best_d[i] = d;
			break;
		}
}

int
best_moves_print(board_t *b, char *str, coord_t *best_c, int nbest)
{
	fprintf(stderr, "%s[ ", str);
	for (int i = 0; i < nbest; i++) {
		const char *str = (is_pass(best_c[i]) ? "" : coord2sstr(best_c[i]));
		fprintf(stderr, "%-3s ", str);
	}
	fprintf(stderr, "]\n");
	return strlen(str);
}
