#ifndef PACHI_CHAT_H
#define PACHI_CHAT_H

#include <stdbool.h>

#include "stone.h"
#include "move.h"

struct board;

void chat_init(char *chat_file);
void chat_done();

char *generic_chat(struct board *b, bool opponent, char *from, char *cmd, enum stone color, coord_t move,
		   int playouts, int machines, int threads, double winrate, double extra_komi);

#endif
