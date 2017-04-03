#ifndef PACHI_PREDICT_PREDICT_H
#define PACHI_PREDICT_PREDICT_H

/* See if engine guesses move m, and return stats string from time to time.
 * Returned string must be freed */
char *predict_move(struct board *b, struct engine *engine, struct time_info *ti, struct move *m);

#endif
