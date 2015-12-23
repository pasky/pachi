#ifndef PACHI_TACTICS_NAKADE_H
#define PACHI_TACTICS_NAKADE_H

/* Piercing eyes. */

#include "board.h"
#include "debug.h"

/* Find an eye-piercing point within the @around area of empty board
 * internal to group of color @color.
 * Returns pass if the area is not a nakade shape or not internal. */
coord_t nakade_point(struct board *b, coord_t around, enum stone color);

/* big eyespace can be reduced to one eye */
bool nakade_dead_shape(struct board *b, coord_t around, enum stone color);

#endif
