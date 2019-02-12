#ifndef PACHI_PREDICT_PREDICT_H
#define PACHI_PREDICT_PREDICT_H

/* See if engine guesses move m, and return stats string from time to time.
 * Returned string must be freed */
char *predict_move(board_t *b, engine_t *e, time_info_t *ti, move_t *m, int games);

#endif
