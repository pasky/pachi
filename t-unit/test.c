#define DEBUG
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "tactics/selfatari.h"
#include "random.h"

static bool board_printed;

void
board_load(struct board *b, FILE *f, unsigned int size)
{
	board_printed = false;
	board_resize(b, size);
	board_clear(b);
	for (int y = size - 1; y >= 0; y--) {
		char line[256];
		if (!fgets(line, sizeof(line), f)) {
			fprintf(stderr, "Premature EOF.\n");
			exit(EXIT_FAILURE);
		}
		line[strlen(line) - 1] = 0; // chomp
		if (strlen(line) != size) {
			fprintf(stderr, "Line not %d char long: %s\n", size, line);
			exit(EXIT_FAILURE);
		}
		for (unsigned int x = 0; x < size; x++) {
			enum stone s;
			switch (line[x]) {
				case '.': s = S_NONE; break;
				case 'X': s = S_BLACK; break;
				case 'O': s = S_WHITE; break;
				default: fprintf(stderr, "Invalid stone %c\n", line[x]);
					 exit(EXIT_FAILURE);
			}
			if (s == S_NONE) continue;
			struct move m = { .color = s, .coord = coord_xy(b, x + 1, y + 1) };
			if (board_play(b, &m) < 0) {
				fprintf(stderr, "Failed to play %s %s\n",
					stone2str(s), coord2sstr(m.coord, b));
				board_print(b, stderr);
				exit(EXIT_FAILURE);
			}
		}
	}
	if (DEBUGL(2))
		board_print(b, stderr);
}

void
test_sar(struct board *b, char *arg)
{
	enum stone color = str2stone(arg);
	arg += 2;
	coord_t *cc = str2coord(arg, board_size(b));
	coord_t c = *cc; coord_done(cc);
	arg += strcspn(arg, " ") + 1;
	int eres = atoi(arg);
	if (DEBUGL(1))
		printf("sar %s %s %d...\t", stone2str(color), coord2sstr(c, b), eres);

	int rres = is_bad_selfatari(b, color, c);

	if (rres == eres) {
		if (DEBUGL(1))
			printf("OK\n");
	} else {
		if (debug_level <= 2) {
			if (DEBUGL(0) && !board_printed) {
				board_print(b, stderr);
				board_printed = true;
			}
			printf("sar %s %s %d...\t", stone2str(color), coord2sstr(c, b), eres);
		}
		printf("FAILED (%d)\n", rres);
	}

}

void
unittest(char *filename)
{
	FILE *f = fopen(filename, "r");
	if (!f) {
		perror(filename);
		exit(EXIT_FAILURE);
	}

	struct board *b = board_init(NULL);
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		line[strlen(line) - 1] = 0; // chomp
		switch (line[0]) {
			case '%': printf("\n%s\n", line); continue;
			case 0: continue;
		}
		if (!strncmp(line, "boardsize ", 10)) {
			board_load(b, f, atoi(line + 10));
		} else if (!strncmp(line, "sar ", 4)) {
			test_sar(b, line + 4);
		} else {
			fprintf(stderr, "Syntax error: %s\n", line);
			exit(EXIT_FAILURE);
		}
	}

	fclose(f);
}
