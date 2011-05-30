#ifndef PACHI_TACTICS_2LIB_H
#define PACHI_TACTICS_2LIB_H

/* Two-liberty tactical checks (i.e. dealing with two-step capturing races,
 * preventing atari). */

#include "board.h"
#include "libmap.h"

void can_atari_group(struct board *b, group_t group, enum stone owner, enum stone to_play, struct libmap_mq *q, int tag, struct libmap_group lmg, bool use_def_no_hopeless);
void group_2lib_check(struct board *b, group_t group, enum stone to_play, struct libmap_mq *q, int tag, bool use_miaisafe, bool use_def_no_hopeless);

#endif
