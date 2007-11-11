#ifndef ZZGO_STONE_H
#define ZZGO_STONE_H

enum stone {
	S_NONE,
	S_BLACK,
	S_WHITE,
	S_MAX,
};

static char stone2char(enum stone s);
char *stone2str(enum stone s); /* static string */
enum stone str2stone(char *str);

static enum stone stone_other(enum stone s);


static inline char
stone2char(enum stone s)
{
	return ".XO"[s];
}

static inline enum stone
stone_other(enum stone s)
{
	switch (s) {
		case S_BLACK: return S_WHITE;
		case S_WHITE: return S_BLACK;
		default: return s;
	}
}

#endif
