#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "fbook.h"

struct fbook *
fbook_init(char *filename, struct board *b)
{
	FILE *f = fopen(filename, "r");
	if (!f) {
		perror(filename);
		return NULL;
	}

	struct fbook *fbook = calloc(1, sizeof(*fbook));
	fbook->bsize = board_size(b);
	fbook->handicap = b->handicap;
	for (int i = 0; i < 1<<fbook_hash_bits; i++)
		fbook->moves[i] = pass;

	if (DEBUGL(1))
		fprintf(stderr, "Loading opening fbook %s...\n", filename);

	/* Scratch board where we lay out the sequence;
	 * one for each transposition. */
	struct board *bs[8];
	for (int i = 0; i < 8; i++) {
		bs[i] = board_init(NULL);
		board_resize(bs[i], fbook->bsize - 2);
	}

	char linebuf[1024];
	while (fgets(linebuf, sizeof(linebuf), f)) {
		char *line = linebuf;
		linebuf[strlen(linebuf) - 1] = 0; // chop

		/* Format of line is:
		 * BSIZE COORD COORD COORD... | COORD
		 * We descend up to |, then add the new node
		 * with value minimax(1000), forcing UCT to
		 * always pick that node immediately. */
		int bsize = strtol(line, &line, 10);
		if (bsize != fbook->bsize - 2)
			continue;
		while (isspace(*line)) line++;

		for (int i = 0; i < 8; i++) {
			board_clear(bs[i]);
			bs[i]->last_move.color = S_WHITE;
		}

		while (*line != '|') {
			coord_t *c = str2coord(line, fbook->bsize);

			for (int i = 0; i < 8; i++) {
#define HASH_VMIRROR     1
#define HASH_HMIRROR     2
#define HASH_XYFLIP      4
				coord_t coord = *c;
				if (i & HASH_VMIRROR) {
					coord = coord_xy(b, coord_x(coord, b), board_size(b) - 1 - coord_y(coord, b));
				}
				if (i & HASH_HMIRROR) {
					coord = coord_xy(b, board_size(b) - 1 - coord_x(coord, b), coord_y(coord, b));
				}
				if (i & HASH_XYFLIP) {
					coord = coord_xy(b, coord_y(coord, b), coord_x(coord, b));
				}
				struct move m = { .coord = coord, .color = stone_other(bs[i]->last_move.color) };
				int ret = board_play(bs[i], &m);
				assert(ret >= 0);
			}

			coord_done(c);
			while (!isspace(*line)) line++;
			while (isspace(*line)) line++;
		}

		line++;
		while (isspace(*line)) line++;

		coord_t *c = str2coord(line, fbook->bsize);
		for (int i = 0; i < 8; i++) {
#if 0
			fprintf(stderr, "%c %"PRIhash" (<%d> %s)\n",
			        is_pass(fbook->moves[bs[i]->hash & fbook_hash_mask]) ? '+' : 'C',
			        bs[i]->hash & fbook_hash_mask, i, linebuf);
#endif
			fbook->moves[bs[i]->hash & fbook_hash_mask] = *c;
		}
		coord_done(c);
	}

	for (int i = 0; i < 8; i++) {
		board_done(bs[i]);
	}

	fclose(f);

	return fbook;
}

void fbook_done(struct fbook *fbook)
{
	free(fbook);
}
