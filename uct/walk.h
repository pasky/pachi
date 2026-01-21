#ifndef PACHI_UCT_WALK_H
#define PACHI_UCT_WALK_H

void uct_progress_status(uct_t *u, tree_t *t, board_t *b, enum stone color, int playouts, coord_t *final);

int uct_playouts(uct_t *u, board_t *b, enum stone color, tree_t *t, time_info_t *ti);

#endif
