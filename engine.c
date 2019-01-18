#define DEBUG
#include "engine.h"


void
engine_init(struct engine *e, int id, char *e_arg, struct board *b)
{
	pachi_engine_init(e, id, e_arg, b);
}

void
engine_done(struct engine *e)
{
	if (e->done) e->done(e);
	if (e->data) free(e->data);
}

struct engine*
new_engine(int id, char *e_arg, struct board *b)
{
	struct engine *e = malloc2(sizeof(*e));
	engine_init(e, id, e_arg, b);
	return e;
}

void
engine_reset(struct engine *e, struct board *b, char *e_arg)
{
	int engine_id = e->id;
	b->es = NULL;
	engine_done(e);
	engine_init(e, engine_id, e_arg, b);
}

void
engine_board_print(struct engine *e, struct board *b, FILE *f)
{
	(e->board_print ? e->board_print(e, b, f) : board_print(b, f));
}

void
engine_best_moves(struct engine *e, struct board *b, struct time_info *ti, enum stone color, 
		  coord_t *best_c, float *best_r, int nbest)
{
	for (int i = 0; i < nbest; i++) {
		best_c[i] = pass;  best_r[i] = 0;
	}
	e->best_moves(e, b, ti, color, best_c, best_r, nbest);
}

struct ownermap*
engine_ownermap(struct engine *e, struct board *b)
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
best_moves_print(struct board *b, char *str, coord_t *best_c, int nbest)
{
	fprintf(stderr, "%s[ ", str);
	for (int i = 0; i < nbest; i++) {
		char *str = (is_pass(best_c[i]) ? "" : coord2sstr(best_c[i], b));
		fprintf(stderr, "%-3s ", str);
	}
	fprintf(stderr, "]\n");
	return strlen(str);
}
