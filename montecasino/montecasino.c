#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "engine.h"
#include "move.h"
#include "montecasino/montecasino.h"
#include "random.h"


/* This is a monte-carlo-based engine with additional per-move heuristics and
 * some feedback mechanisms. It is based on montecarlo/, with some enhancements
 * that would make it too convoluted already. It plays MC_GAMES "random" games
 * from the current board and records win/loss ratio for each first move.
 * The move with the biggest number of winning games gets played. */
/* Note that while the library is based on New Zealand rules, this engine
 * returns moves according to Chinese rules. Thus, it does not return suicide
 * moves. It of course respects positional superko too. */

/* Pass me arguments like a=b,c=d,...
 * Supported arguments:
 * debug[=DEBUG_LEVEL]		1 is the default; more means more debugging prints
 * games=MC_GAMES		number of random games to play
 * gamelen=MC_GAMELEN		maximal length of played random game
 *
 * The following arguments tune domain-specific heuristics. They tend to carry
 * very high performance penalty.
 * pure				turns all the heuristics off; you can then turn
 * 				them on selectively
 * atari_rate=MC_ATARIRATE	how many of 100 moves should be non-random but
 * 				fix local atari, if there is any
 * local_rate=MC_LOCALRATE	how many of 100 moves should be contact plays
 * 				(tsuke or diagonal)
 * cut_rate=MC_CUTRATE		how many of 100 moves should fix local cuts,
 * 				if there are any */


/* Times for 10000 runs on 1.6GHz Athlon. pure runs at ~500ms */

#define MC_GAMES	40000
#define MC_GAMELEN	400
#define MC_ATARIRATE	50 /* +200ms */
#define MC_CUTRATE	40 /* +100ms */
#define MC_LOCALRATE	30 /* +100ms */


/* FIXME: Cutoff rule for simulations. Currently we are so fast that this
 * simply does not matter; even 100000 simulations are fast enough to
 * play 5 minutes S.D. on 19x19 and anything more sounds too ridiculous
 * already. */
/* FIXME: We cannot handle seki. Any good ideas are welcome. A possibility is
 * to consider 'pass' among the moves, but this seems tricky. */


struct montecasino {
	int debug_level;
	int games, gamelen;
	int atari_rate, local_rate, cut_rate;
	float resign_ratio;
	int loss_threshold;

	coord_t last_hint;
	int last_hint_value;
};

/* Per-move playout statistics. */
struct move_stat {
	int games;
	int wins;
};


static void
board_stats_print(struct board *board, struct move_stat *moves, FILE *f)
{
	fprintf(f, "\n       ");
	int x, y;
	char asdf[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
	for (x = 1; x < board->size - 1; x++)
		fprintf(f, "%c    ", asdf[x - 1]);
	fprintf(f, "\n   +-");
	for (x = 1; x < board->size - 1; x++)
		fprintf(f, "-----");
	fprintf(f, "+\n");
	for (y = board->size - 2; y >= 1; y--) {
		fprintf(f, "%2d | ", y);
		for (x = 1; x < board->size - 1; x++)
			if (moves[y * board->size + x].games)
				fprintf(f, "%0.2f ", (float) moves[y * board->size + x].wins / moves[y * board->size + x].games);
			else
				fprintf(f, "---- ");
		fprintf(f, "| ");
		for (x = 1; x < board->size - 1; x++)
			fprintf(f, "%4d ", moves[y * board->size + x].games);
		fprintf(f, "|\n");
	}
	fprintf(f, "   +-");
	for (x = 1; x < board->size - 1; x++)
		fprintf(f, "-----");
	fprintf(f, "+\n");
}


/* *** Domain-specific knowledge comes here (that is, any heuristics that perfer
 * certain moves, aside of requiring the moves to be according to the rules). */
/* NOTE: This heuristics affects ONLY the random playouts! It does not help the
 * engine directly to pick a move, but it makes it pick the hinted moves in the
 * random playouts FROM the random initial move. So the engine will not prefer
 * to fix atari on the current board, but it will fix it as the other player
 * when the next move on current board failed to deal with it. */

static coord_t
domain_hint_atari(struct montecasino *mc, struct board *b, coord_t coord)
{
	/* If we or our neighbors are in atari, fix that, (Capture or escape.)
	 * This test costs a lot of performance (the whole playout is about 1/4
	 * slower), but improves the playouts a lot. */

	if (unlikely(mc->debug_level > 8)) {
		fprintf(stderr, "-- Scanning for %d,%d-urgent moves:\n", coord_x(coord), coord_y(coord));
		board_print(b, stderr);
	}

	coord_t urgents[5]; int urgents_len = 0;
	memset(urgents, 0, sizeof(urgents));

	coord_t fix;
	if (unlikely(board_group_in_atari(b, group_at(b, coord), &fix)))
		urgents[urgents_len++] = fix;
	foreach_neighbor(b, coord) {
		/* This can produce duplicate candidates. But we should prefer
		 * bigger groups to smaller ones, so I guess that is kinda ok. */
		if (likely(group_at(b, c)) && unlikely(board_group_in_atari(b, group_at(b, c), &fix)))
			urgents[urgents_len++] = fix;
	} foreach_neighbor_end;

	if (unlikely(urgents_len)) {
		if (unlikely(mc->debug_level > 8)) {
			fprintf(stderr, "Urgent moves found:");
			int i = 0;
			for (i = 0; i < urgents_len; i++)
				fprintf(stderr, " %d,%d", coord_x(urgents[i]), coord_y(urgents[i]));
			fprintf(stderr, "\n");
		}
		return urgents[fast_random(urgents_len)];
	}
	return pass;
}

static coord_t
domain_hint_cut(struct montecasino *mc, struct board *b, coord_t coord)
{
	/* Check if this move is cutting kosumi:
	 * (O) X
	 *  X  .  */

	if (unlikely(mc->debug_level > 8)) {
		fprintf(stderr, "-- Scanning for %d,%d-cut moves:\n", coord_x(coord), coord_y(coord));
		board_print(b, stderr);
	}

	coord_t cuts[4]; int cuts_len = 0;
	memset(cuts, 0, sizeof(cuts));

	enum stone cutting_color = stone_other(board_at(b, coord));
	foreach_diag_neighbor(b, coord) {
		if (board_at(b, c) == S_NONE) {
			if (neighbor_count_at(b, c, cutting_color) != 2) {
				/* Either this isn't a cut or the opponent has
				 * too many friends there. */
				continue;
			}

			/* XXX: Some internal board specific magic here... */
			coord_t cutted = coord;
			if (coord_x(c) < coord_x(coord))
				cutted.pos--;
			else
				cutted.pos++;
			if (likely(board_at(b, cutted) != cutting_color))
				continue;
			cutted.pos = c.pos;
			if (coord_y(c) < coord_y(coord))
				cutted.pos -= cutted.size;
			else
				cutted.pos += cutted.size;
			if (likely(board_at(b, cutted) != cutting_color))
				continue;
			/* Cut kosumi! */
			cuts[cuts_len++] = c;
		}
	} foreach_diag_neighbor_end;

	if (unlikely(cuts_len)) {
		if (unlikely(mc->debug_level > 8)) {
			fprintf(stderr, "Cutting moves found:");
			int i = 0;
			for (i = 0; i < cuts_len; i++)
				fprintf(stderr, " %d,%d", coord_x(cuts[i]), coord_y(cuts[i]));
			fprintf(stderr, "\n");
		}
		return cuts[fast_random(cuts_len)];
	}
	return pass;
}

static coord_t
domain_hint_local(struct montecasino *mc, struct board *b, coord_t coord)
{
	/* Pick a suitable move that is directly or diagonally adjecent. In the
	 * real game, local moves often tend to be the urgent ones, even if they
	 * aren't atari. */

	if (unlikely(mc->debug_level > 8)) {
		fprintf(stderr, "-- Scanning for %d,%d-local moves:\n", coord_x(coord), coord_y(coord));
		board_print(b, stderr);
	}

	coord_t neis[S_MAX][8]; int neis_len[S_MAX];
	memset(neis, 0, sizeof(neis));
	memset(neis_len, 0, sizeof(neis_len));

	foreach_neighbor(b, coord) {
		neis[(enum stone) board_at(b, c)][neis_len[(enum stone) board_at(b, c)]++] = c;
	} foreach_neighbor_end;

	foreach_diag_neighbor(b, coord) {
		neis[(enum stone) board_at(b, c)][neis_len[(enum stone) board_at(b, c)]++] = c;
	} foreach_diag_neighbor_end;

	if (likely(neis_len[S_NONE])) {
		if (unlikely(mc->debug_level > 8)) {
			fprintf(stderr, "Local moves found:");
			int i = 0;
			for (i = 0; i < neis_len[S_NONE]; i++)
				fprintf(stderr, " %d,%d", coord_x(neis[S_NONE][i]), coord_y(neis[S_NONE][i]));
			fprintf(stderr, "\n");
		}
		return neis[S_NONE][fast_random(neis_len[S_NONE])];
	}
	return pass;
}

static void
domain_hint(struct montecasino *mc, struct board *b, coord_t *urgent, enum stone our_real_color)
{
	if (is_pass(b->last_move.coord))
		return;

	/* Now now, if we ignored an urgent move, the opponent will
	 * take it! */
	/* Note that we should use this only when the _REAL_ us tenukies
	 * and the _REAL_ opponent comes back. Otherwise we hope in
	 * opponent's tenuki too much and play out ladders. :-) */
	if (!is_pass(mc->last_hint)
	    && unlikely(!coord_eq(b->last_move.coord, mc->last_hint))
	    && b->last_move.color == our_real_color
	    && fast_random(100) < mc->last_hint_value) {
		*urgent = mc->last_hint;
		mc->last_hint = pass;
		return;
	}

	/* In some of the cases, we pick atari response instead of random move.
	 * If there is an atari, capturing tends to be huge. */
	if (mc->atari_rate && fast_random(100) < mc->atari_rate) {
		*urgent = domain_hint_atari(mc, b, b->last_move.coord);
		if (!is_pass(*urgent)) {
			mc->last_hint = *urgent;
			mc->last_hint_value = mc->atari_rate;
			return;
		}
	}

	/* Cutting is kinda urgent, too. */
	if (mc->cut_rate && fast_random(100) < mc->cut_rate) {
		*urgent = domain_hint_cut(mc, b, b->last_move.coord);
		if (!is_pass(*urgent)) {
			mc->last_hint = *urgent;
			mc->last_hint_value = mc->cut_rate;
			return;
		}
	}

	/* For the non-urgent moves, some of them will be contact play (tsuke
	 * or diagonal). These tend to be likely urgent. */
	if (mc->local_rate && fast_random(100) < mc->local_rate) {
		*urgent = domain_hint_local(mc, b, b->last_move.coord);
		if (!is_pass(*urgent)) {
			mc->last_hint = *urgent;
			mc->last_hint_value = mc->local_rate;
			return;
		}
	}

	mc->last_hint = pass;
}

/* 1: m->color wins, 0: m->color loses; -1: no moves left
 * -2 superko inside the game tree (NOT at root, that's simply invalid move)
 * -3 first move is multi-stone suicide */
static int
play_random_game(struct montecasino *mc, struct board *b, struct move_stat *moves,
		 struct move *m, int i)
{
	struct board b2;
	board_copy(&b2, b);

	board_play_random(&b2, m->color, &m->coord);
	if (is_pass(m->coord) || b->superko_violation) {
		if (mc->debug_level > 3)
			fprintf(stderr, "\tno moves left\n");
		board_done_noalloc(&b2);
		return -1;
	}
	if (!group_at(&b2, m->coord)) {
		if (mc->debug_level > 4) {
			fprintf(stderr, "SUICIDE DETECTED at %d,%d:\n", coord_x(m->coord), coord_y(m->coord));
			board_print(&b2, stderr);
		}
		return -3;
	}

	if (mc->debug_level > 3)
		fprintf(stderr, "[%d,%d] playing random game\n", coord_x(m->coord), coord_y(m->coord));

	int gamelen = mc->gamelen - b2.moves;
	if (gamelen < 10)
		gamelen = 10;

	enum stone color = stone_other(m->color);
	coord_t next_move = pass;
	coord_t urgent;

	int passes = 0;

	/* Special check: We probably tenukied the last opponent's move. But
	 * check if the opponent has lucrative local continuation for her last
	 * move! */
	/* This check is ultra-important BTW. Without it domain checking does
	 * not bring that much of an advantage. It might even warrant it to by
	 * default do only this domain check. */
	urgent = pass;
	domain_hint(mc, b, &urgent, m->color);
	if (!is_pass(urgent))
		goto play_urgent;

	while (gamelen-- && passes < 2) {
		urgent = pass;
		domain_hint(mc, &b2, &urgent, m->color);

		coord_t coord;

		if (!is_pass(urgent)) {
			struct move m;
play_urgent:
			m.coord = urgent; m.color = color;
			if (board_play(&b2, &m) < 0) {
				if (unlikely(mc->debug_level > 7)) {
					fprintf(stderr, "Urgent move %d,%d is ILLEGAL:\n", coord_x(urgent), coord_y(urgent));
					board_print(&b2, stderr);
				}
				goto play_random;
			}
			coord = urgent;
		} else {
play_random:
			board_play_random(&b2, color, &coord);
		}

		if (unlikely(mc->debug_level > 2) && is_pass(next_move))
			next_move = coord;

		if (unlikely(b2.superko_violation)) {
			/* We ignore superko violations that are suicides. These
			 * are common only at the end of the game and are
			 * rather harmless. (They will not go through as a root
			 * move anyway.) */
			if (group_at(&b2, coord)) {
				if (unlikely(mc->debug_level > 3)) {
					fprintf(stderr, "Superko fun at %d,%d in\n", coord_x(coord), coord_y(coord));
					if (mc->debug_level > 4)
						board_print(&b2, stderr);
				}
				board_done_noalloc(&b2);
				return -2;
			} else {
				if (unlikely(mc->debug_level > 6)) {
					fprintf(stderr, "Ignoring superko at %d,%d in\n", coord_x(coord), coord_y(coord));
					board_print(&b2, stderr);
				}
				b2.superko_violation = false;
			}
		}

		if (unlikely(mc->debug_level > 7)) {
			char *cs = coord2str(coord);
			fprintf(stderr, "%s %s\n", stone2str(color), cs);
			free(cs);
		}

		if (unlikely(is_pass(coord))) {
			passes++;
		} else {
			passes = 0;
		}

		color = stone_other(color);
	}

	if (mc->debug_level > 6 - !(i % (mc->games/2)))
		board_print(&b2, stderr);

	float score = board_fast_score(&b2);
	bool result = (m->color == S_WHITE ? (score > 0 ? 1 : 0) : (score < 0 ? 1 : 0));

	if (mc->debug_level > 3) {
		fprintf(stderr, "\tresult %d (score %f)\n", result, score);
	}

	if (unlikely(mc->debug_level > 2)) {
		int i = m->coord.pos * b->size2 + next_move.pos;
		moves[i].games++;
		if (!result)
			moves[i].wins++;
	}

	board_done_noalloc(&b2);
	return result;
}


static coord_t *
montecasino_genmove(struct engine *e, struct board *b, enum stone color)
{
	struct montecasino *mc = e->data;
	struct move m;
	m.color = color;

	/* resign when the hope for win vanishes */
	coord_t top_coord = resign;
	float top_ratio = mc->resign_ratio;

	struct move_stat moves[b->size2];
	memset(moves, 0, sizeof(moves));

	struct move_stat second_moves[b->size2][b->size2];
	if (mc->debug_level > 2) {
		memset(second_moves, 0, sizeof(second_moves));
	}

	int losses = 0;
	int i, superko = 0, good_games = 0;
	for (i = 0; i < mc->games; i++) {
		int result = play_random_game(mc, b, (struct move_stat *) second_moves, &m, i);

		if (result == -1) {
pass_wins:
			/* No more moves. */
			top_coord = pass; top_ratio = 0.5;
			goto move_found;
		}
		if (result == -2) {
			/* Superko. We just ignore this playout.
			 * And play again. */
			if (unlikely(superko > 2 * MC_GAMES)) {
				/* Uhh. Triple ko, or something? */
				if (mc->debug_level > 0)
					fprintf(stderr, "SUPERKO LOOP. I will pass. Did we hit triple ko?\n");
				goto pass_wins;
			}
			/* This playout didn't count; we should not
			 * disadvantage moves that lead to a superko.
			 * And it is supposed to be rare. */
			i--, superko++;
			continue;
		}
		if (result == -3) {
			/* Multi-stone suicide. We play chinese rules,
			 * so we can't consider this. (Note that we
			 * unfortunately still consider this in playouts.) */
			continue;
		}

		good_games++;
		moves[m.coord.pos].games++;

		if (b->moves < 3) {
			/* Simple heuristic: avoid opening too low. Do not
			 * play on second or first line as first white or
			 * first two black moves.*/
			if (coord_x(m.coord) < 3 || coord_x(m.coord) > b->size - 4
			    || coord_y(m.coord) < 3 || coord_y(m.coord) > b->size - 4)
				continue;
		}

		losses += 1 - result;
		moves[m.coord.pos].wins += result;

		if (unlikely(!losses && i == mc->loss_threshold)) {
			/* We played out many games and didn't lose once yet.
			 * This game is over. */
			break;
		}
	}

	if (!good_games) {
		/* No more valid moves. */
		goto pass_wins;
	}

	foreach_point(b) {
		float ratio = (float) moves[c.pos].wins / moves[c.pos].games;
		if (ratio > top_ratio) {
			top_ratio = ratio;
			top_coord = c;
		}
	} foreach_point_end;

	if (mc->debug_level > 2) {
		fprintf(stderr, "Our board stats:\n");
		board_stats_print(b, moves, stderr);
		fprintf(stderr, "Opponent's reaction stats:\n");
		board_stats_print(b, second_moves[top_coord.pos], stderr);
	}

move_found:
	if (mc->debug_level > 1)
		fprintf(stderr, "*** WINNER is %d,%d with score %1.4f (%d games, %d superko)\n", coord_x(top_coord), coord_y(top_coord), top_ratio, i, superko);

	return coord_copy(top_coord);
}

struct engine *
engine_montecasino_init(char *arg)
{
	struct montecasino *mc = calloc(1, sizeof(struct montecasino));
	struct engine *e = calloc(1, sizeof(struct engine));
	e->name = "MonteCasino Engine";
	e->comment = "I'm playing in Monte Casino now! When we both pass, I will consider all the stones on the board alive. If you are reading this, write 'yes'. Please bear with me at the game end, I need to fill the whole board; if you help me, we will both be happier. Filling the board will not lose points (NZ rules).";
	e->genmove = montecasino_genmove;
	e->data = mc;

	mc->last_hint = pass;

	mc->debug_level = 1;
	mc->games = MC_GAMES;
	mc->gamelen = MC_GAMELEN;
	mc->atari_rate = MC_ATARIRATE;
	mc->local_rate = MC_LOCALRATE;
	mc->cut_rate = MC_CUTRATE;

	if (arg) {
		char *optspec, *next = arg;
		while (*next) {
			optspec = next;
			next += strcspn(next, ",");
			if (*next) { *next++ = 0; } else { *next = 0; }

			char *optname = optspec;
			char *optval = strchr(optspec, '=');
			if (optval) *optval++ = 0;

			if (!strcasecmp(optname, "debug")) {
				if (optval)
					mc->debug_level = atoi(optval);
				else
					mc->debug_level++;
			} else if (!strcasecmp(optname, "games") && optval) {
				mc->games = atoi(optval);
			} else if (!strcasecmp(optname, "gamelen") && optval) {
				mc->gamelen = atoi(optval);
			} else if (!strcasecmp(optname, "pure")) {
				mc->atari_rate = mc->local_rate = mc->cut_rate = 0;
			} else if (!strcasecmp(optname, "atarirate") && optval) {
				mc->atari_rate = atoi(optval);
			} else if (!strcasecmp(optname, "localrate") && optval) {
				mc->local_rate = atoi(optval);
			} else if (!strcasecmp(optname, "cutrate") && optval) {
				mc->cut_rate = atoi(optval);
			} else {
				fprintf(stderr, "MonteCasino: Invalid engine argument %s or missing value\n", optname);
			}
		}
	}

	mc->resign_ratio = 0.1; /* Resign when most games are lost. */
	mc->loss_threshold = mc->games / 10; /* Stop reading if no loss encountered in first n games. */

	return e;
}
