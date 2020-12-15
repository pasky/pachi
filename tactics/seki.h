#ifndef PACHI_TACTICS_SEKI_H
#define PACHI_TACTICS_SEKI_H

#define MOGGY_MIDDLEGAME  (board_rsize2(b) * 10 / 25)      /*  19x19: 144 */
#define MOGGY_ENDGAME     (board_rsize2(b) * 100 / 164)    /*  19x19: 220 */

#define check_special_sekis(b, m)  \
	(b->moves > MOGGY_MIDDLEGAME && !immediate_liberty_count(b, m->coord))

#define check_endgame_sekis(b, m, random_move) \
	(b->moves > MOGGY_ENDGAME && (random_move))

bool breaking_local_seki(board_t *b, selfatari_state_t *s, group_t c);
bool breaking_false_eye_seki(board_t *b, coord_t coord, enum stone color);
bool breaking_3_stone_seki(board_t *b, coord_t coord, enum stone color);

#endif /* PACHI_TACTICS_SEKI_H */
