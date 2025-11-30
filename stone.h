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
	return ".XO#"[s];
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
	assert(0);
}

/* Curiously, gcc is reluctant to inline this; I have confirmed
 * there is performance benefit. */
static inline enum stone __attribute__((always_inline))
stone_other(enum stone s)
{
	return (S_OFFBOARD - s);
}

#endif
