#ifndef PACHI_TACTICS_SEKI_H
#define PACHI_TACTICS_SEKI_H

bool breaking_3_stone_seki(struct board *b, coord_t coord, enum stone color);
bool breaking_corner_seki(struct board *b, coord_t coord, enum stone color);
bool breaking_false_eye_seki(struct board *b, coord_t coord, enum stone color);

#endif /* PACHI_TACTICS_SEKI_H */
