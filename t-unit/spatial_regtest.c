#define DEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "timeinfo.h"
#include "engine.h"
#include "gtp.h"
#include "patternsp.h"
#include "patternprob.h"

static void
fake_ownermap(board_t *b, ownermap_t *ownermap)
{
	int games = 100;
	
	ownermap_init(ownermap);
	foreach_point(b) {
		enum stone color = board_at(b, c);
		if (color == S_OFFBOARD)  continue;
		ownermap->map[c][S_NONE] += games;
	} foreach_point_end;
	ownermap->playouts += games;
}

static void
dump_spatials(board_t *b, pattern_config_t *pc)
{
	/* Skip suicides */
	if (b->moves && board_at(b, last_move(b).coord) == S_NONE)  return;

	//board_print(b, stderr);

	enum stone color = (is_pass(last_move(b).coord) ? S_BLACK : stone_other(last_move(b).color));
	pattern_t pats[b->flen];
	floating_t probs[b->flen];
	ownermap_t ownermap;  fake_ownermap(b, &ownermap);  /* fake */
	pattern_rate_moves(pc, b, color, pats, probs, &ownermap);
	
	for (int f = 0; f < b->flen; f++) {
		if (isnan(probs[f]))  continue;
		fprintf(stderr, "move %i: %s: ", b->moves, coord2sstr(b->f[f]));
		
		pattern_t *pat = &pats[f];
		for (int i = 0; i < pat->n; i++) {
			if (pat->f[i].id < FEAT_SPATIAL3) continue;
			char s[256]; feature2str(s, &pat->f[i]);
			fprintf(stderr, "%s ", s);
		}
		fprintf(stderr, "\n");
	}
}

/* Replay games dumping spatials every 10 moves. */
bool
spatial_regression_test(board_t *b, char *arg)
{
	time_info_t ti[S_MAX];
	ti[S_BLACK] = ti_none;
	ti[S_WHITE] = ti_none;

	pattern_config_t pc;
	patterns_init(&pc, NULL, false, true);

	gtp_t gtp;  gtp_init(&gtp);
	char buf[4096];
	engine_t e;  memset(&e, 0, sizeof(e));  /* dummy engine */
	while (fgets(buf, 4096, stdin)) {
		if (buf[0] == '#') continue;
		//if (!strncmp(buf, "clear_board", 11))  printf("\nGame %i:\n", gtp.played_games + 1);
		// if (DEBUGL(1))  fprintf(stderr, "IN: %s", buf);

		gtp_parse(&gtp, b, &e, NULL, ti, buf);
		if (b->moves % 10 == 1)
			dump_spatials(b, &pc);
		b->superko_violation = false;       // never cleared currently.
	}

	return true;
}
