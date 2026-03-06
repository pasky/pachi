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
group_t dragon_at(board_t *b, coord_t to);

/* Return whether all groups in @groups belong to the same dragon. */
bool same_dragon_groups(board_t *b, mq_t *groups);

/* Returns total number of liberties of dragon at @to. */
int dragon_liberties(board_t *b, enum stone color, coord_t to);

/* Print board highlighting given dragon */
void dragon_print(board_t *board, FILE *f, group_t dragon);

/* Like board_print() but use a different color for each dragon */
void board_print_dragons(board_t *board, FILE *f);

/* Pick a color for dragon with index @i. Returns ansi color code.
 * Useful for writing custom board_print_dragons()-like functions. */
char *pick_dragon_color(int i, bool bold, bool white_ok);

/* Find if dragon has 2 eyes.
 * Limitations:
 * - big eye areas must be completely enclosed and have all surrounded stones connected.
 * - doesn't take prisoners' shape into account (won't count extra eye if prisoners
 *   can't be connected because of suicide). */
bool dragon_is_safe(board_t *b, group_t g, enum stone color);

/* Does one opposite color group neighbor of @g have 2 eyes ? */
bool neighbor_is_safe(board_t *b, group_t g);

/* Try to detect big eye area, ie:
 *  - completely enclosed area, not too big,
 *  - surrounding stones all connected to each other
 *  - size >= 2  (so no false eye issues)
 * Returns size of the area, or 0 if doesn't match.  */
int big_eye_area(board_t *b, enum stone color, coord_t around, mq_t *area);

/* Point we control: 
 * Opponent can't play there or we can capture if he does. */
bool is_controlled_eye_point(board_t *b, coord_t to, enum stone color);

/* Try to find out if dragon is completely surrounded:
 * Look for outwards 2-stones gap from our external liberties. 
 * (hack, but works pretty well in practice) */
bool dragon_is_surrounded(board_t *b, coord_t to);


#endif /* PACHI_TACTICS_DRAGON_H */
