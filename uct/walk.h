#ifndef PACHI_UCT_WALK_H
#define PACHI_UCT_WALK_H

#include "move.h"

struct tree;
struct uct;
struct board;

void uct_progress_status(struct uct *u, struct tree *t, enum stone color, int playouts, coord_t *final);

int uct_playout(struct uct *u, struct board *b, enum stone player_color, struct tree *t);
int uct_playouts(struct uct *u, struct board *b, enum stone color, struct tree *t, struct time_info *ti);

#endif
