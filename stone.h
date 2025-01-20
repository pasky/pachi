#ifndef PACHI_STONE_H
#define PACHI_STONE_H

#include <assert.h>

enum stone {
	S_NONE,
	S_BLACK,
	S_WHITE,
	S_OFFBOARD,
	S_MAX,
};

static char stone2char(enum stone s);
static enum stone char2stone(char s);
bool valid_color(char *s);
char *stone2str(enum stone s); /* static string */
enum stone str2stone(char *str);

static enum stone stone_other(enum stone s);


static inline char
stone2char(enum stone s)
{
	/* Hack: stone2char(S_MAX) = ' '
	 *       Allow S_MAX here so can blank-out parts of the
	 *       board in board_print() (see spatial_print()) */
	return ".XO# "[s];
}

static inline enum stone
char2stone(char s)
{
	switch (s) {
		case '.': return S_NONE;
		case 'X': return S_BLACK;
		case 'O': return S_WHITE;
		case '#': return S_OFFBOARD;
	}
	return S_NONE; // XXX
}

/* Curiously, gcc is reluctant to inline this; I have cofirmed
 * there is performance benefit. */
static inline enum stone __attribute__((always_inline))
stone_other(enum stone s)
{
	static const enum stone o[S_MAX] = { S_NONE, S_WHITE, S_BLACK, S_OFFBOARD };
	return o[s];
}

#endif
