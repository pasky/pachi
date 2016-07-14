#ifndef PACHI_TACTICS_DRAGON_H
#define PACHI_TACTICS_DRAGON_H

/* Functions for dealing with dragons, ie virtually connected groups of stones.
 * Used for some high-level tactics decisions, like trying to detect useful lost
 * ladders or whether breaking a 3-stones seki is safe.
 * Currently these are fairly expensive (dragon data is not cached) so shouldn't be
 * called by low-level / perf-critical code. */


/* Like group_at() but returns unique id for all stones in a dragon.
 * Depending on the situation what is considered to be a dragon here may or
 * may not match what we'd intuitively call a dragon: there are connections
 * it doesn't understand (dead cutting stones for instance) so it'll usually
 * be smaller. Doesn't need to be perfect though. */
group_t dragon_at(struct board *b, coord_t to);

/* Returns total number of liberties of dragon at @to. */
int dragon_liberties(struct board *b, enum stone color, coord_t to);

/* Print board highlighting given dragon */
void dragon_print(struct board *board, FILE *f, group_t dragon);

/* Like board_print() but use a different color for each dragon */
void board_print_dragons(struct board *board, FILE *f);

/* Pick a color for dragon with index @i. Returns ansi color code.
 * Useful for writing custom board_print_dragons()-like functions. */
char *pick_dragon_color(int i, bool bold, bool white_ok);

/* Try to find out if dragon has 2 eyes. Pretty conservative:
 * big eye areas are counted as one eye, must be completely enclosed and
 * have all surrounded stones connected. Doesn't need to be perfect though. */
bool dragon_is_safe(struct board *b, group_t g, enum stone color);

/* Like group_is_safe() but passing already visited stones / eyes. */
bool dragon_is_safe_full(struct board *b, group_t g, enum stone color, int *visited, int *eyes);

/* Try to detect big eye area, ie:
 *  - completely enclosed area, not too big,
 *  - surrounding stones all connected to each other
 *  - size >= 2  (so no false eye issues)
 * Returns size of the area, or 0 if doesn't match.  */
int big_eye_area(struct board *b, enum stone color, coord_t around, int *visited);

/* Try to find out if dragon is completely surrounded:
 * Look for outwards 2-stones gap from our external liberties. 
 * (hack, but works pretty well in practice) */
bool dragon_is_surrounded(struct board *b, coord_t to);


#endif /* PACHI_TACTICS_DRAGON_H */
