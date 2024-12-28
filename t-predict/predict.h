#ifndef PACHI_PREDICT_PREDICT_H
#define PACHI_PREDICT_PREDICT_H

/* Check if engine guesses move m, and return stats from time to time. */
char *predict_move(board_t *b, engine_t *e, time_info_t *ti, move_t *m, int games);

#endif
