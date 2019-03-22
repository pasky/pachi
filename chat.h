#ifndef PACHI_CHAT_H
#define PACHI_CHAT_H

#include <stdbool.h>

#include "stone.h"
#include "move.h"

void chat_init(char *chat_file);
void chat_done(void);

char *generic_chat(board_t *b, bool opponent, char *from, char *cmd, enum stone color, coord_t move,
		   int playouts, int machines, int threads, double winrate, double extra_komi, char *score_est);

#endif
