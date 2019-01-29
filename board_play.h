/* board_play() implementation */

static void
board_group_addlib(board_t *board, group_t group, coord_t coord)
{
	if (DEBUGL(7))
		fprintf(stderr, "Group %d[%s] %d: Adding liberty %s\n",
			group_base(group), coord2sstr(group_base(group)),
			board_group_info(board, group).libs, coord2sstr(coord));

	group_info_t *gi = &board_group_info(board, group);
	if (gi->libs < GROUP_KEEP_LIBS) {
		for (int i = 0; i < GROUP_KEEP_LIBS; i++) {
#if 0                   /* Seems extra branch just slows it down */
			if (!gi->lib[i]) break;
#endif
			if (unlikely(gi->lib[i] == coord))
				return;
		}
#ifdef FULL_BOARD
		if      (gi->libs == 0)  board_capturable_add(board, group, coord);
		else if (gi->libs == 1)  board_capturable_rm(board, group, gi->lib[0]);
#endif
		gi->lib[gi->libs++] = coord;
	}
}

static void
board_group_find_extra_libs(board_t *board, group_t group, group_info_t *gi, coord_t avoid)
{
	/* Add extra liberty from the board to our liberty list. */
	unsigned char watermark[board_size2(board) / 8];
	memset(watermark, 0, sizeof(watermark));
#define watermark_get(c)	(watermark[c >> 3] & (1 << (c & 7)))
#define watermark_set(c)	watermark[c >> 3] |= (1 << (c & 7))

	for (int i = 0; i < GROUP_KEEP_LIBS - 1; i++)
		watermark_set(gi->lib[i]);
	watermark_set(avoid);

	foreach_in_group(board, group) {
		coord_t coord2 = c;
		foreach_neighbor(board, coord2, {
			if (board_at(board, c) + watermark_get(c) != S_NONE)
				continue;
			watermark_set(c);
			gi->lib[gi->libs++] = c;
			if (unlikely(gi->libs >= GROUP_KEEP_LIBS))
				return;
		} );
	} foreach_in_group_end;
#undef watermark_get
#undef watermark_set
}

static void
board_group_rmlib(board_t *board, group_t group, coord_t coord)
{
	if (DEBUGL(7))
		fprintf(stderr, "Group %d[%s] %d: Removing liberty %s\n",
			group_base(group), coord2sstr(group_base(group)),
			board_group_info(board, group).libs, coord2sstr(coord));

	group_info_t *gi = &board_group_info(board, group);
	for (int i = 0; i < GROUP_KEEP_LIBS; i++) {
#if 0           /* Seems extra branch just slows it down */
		if (!gi->lib[i]) break;
#endif
		if (likely(gi->lib[i] != coord))
			continue;

#ifdef FULL_BOARD
		coord_t lib =
#endif
			gi->lib[i] = gi->lib[--gi->libs];
		gi->lib[gi->libs] = 0;
		
		/* Postpone refilling lib[] until we need to. */
		assert(GROUP_REFILL_LIBS > 1);
		if (gi->libs > GROUP_REFILL_LIBS)
			return;
		if (gi->libs == GROUP_REFILL_LIBS)
			board_group_find_extra_libs(board, group, gi, coord);
#ifdef FULL_BOARD		
		if      (gi->libs == 1) board_capturable_add(board, group, gi->lib[0]);
		else if (gi->libs == 0) board_capturable_rm(board, group, lib);
#endif
		return;
	}

	/* This is ok even if gi->libs < GROUP_KEEP_LIBS since we
	 * can call this multiple times per coord. */
	return;
}


/* This is a low-level routine that doesn't maintain consistency
 * of all the board data structures. */
static void
board_remove_stone(board_t *board, group_t group, coord_t c)
{
	enum stone color = board_at(board, c);
	board_at(board, c) = S_NONE;
	group_at(board, c) = 0;
#ifdef FULL_BOARD
	board_hash_update(board, c, color);
#endif

	/* Increase liberties of surrounding groups */
	coord_t coord = c;
	foreach_neighbor(board, coord, {
		dec_neighbor_count_at(board, c, color);
		group_t g = group_at(board, c);
		if (g && g != group)
			board_group_addlib(board, g, coord);
	});

#ifdef FULL_BOARD	
	/* board_hash_update() might have seen the freed up point as able
	 * to capture another group in atari that only after the loop
	 * above gained enough liberties. Reset pat3 again. */
	board_pat3_reset(board, c);
	board_addf(board, c);	
#endif
}

static int profiling_noinline
board_group_capture(board_t *board, group_t group)
{
	int stones = 0;

	foreach_in_group(board, group) {
		board->captures[stone_other(board_at(board, c))]++;
		board_remove_stone(board, group, c);
		stones++;
	} foreach_in_group_end;

	group_info_t *gi = &board_group_info(board, group);
	assert(gi->libs == 0);
	memset(gi, 0, sizeof(*gi));

	return stones;
}


static void profiling_noinline
add_to_group(board_t *board, group_t group, coord_t prevstone, coord_t coord)
{
	group_at(board, coord) = group;
	groupnext_at(board, coord) = groupnext_at(board, prevstone);
	groupnext_at(board, prevstone) = coord;

	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			board_group_addlib(board, group, c);
	});

	if (DEBUGL(8))
		fprintf(stderr, "add_to_group: added (%d,%d ->) %d,%d (-> %d,%d) to group %d\n",
			coord_x(prevstone), coord_y(prevstone),
			coord_x(coord), coord_y(coord),
			groupnext_at(board, coord) % board_size(board), groupnext_at(board, coord) / board_size(board),
			group_base(group));
}

static void profiling_noinline
merge_groups(board_t *board, group_t group_to, group_t group_from)
{
	if (DEBUGL(7))
		fprintf(stderr, "board_play_raw: merging groups %d -> %d\n",
			group_base(group_from), group_base(group_to));
	group_info_t *gi_from = &board_group_info(board, group_from);
	group_info_t *gi_to = &board_group_info(board, group_to);
#ifdef FULL_BOARD
	/* We do this early before the group info is rewritten. */
	if (gi_from->libs == 1)  board_capturable_rm(board, group_from, gi_from->lib[0]);
#endif

	if (DEBUGL(7))  fprintf(stderr,"---- (froml %d, tol %d)\n", gi_from->libs, gi_to->libs);

	if (gi_to->libs < GROUP_KEEP_LIBS) {
		for (int i = 0; i < gi_from->libs; i++) {
			for (int j = 0; j < gi_to->libs; j++)
				if (gi_to->lib[j] == gi_from->lib[i])
					goto next_from_lib;
#ifdef FULL_BOARD				
			if      (gi_to->libs == 0)  board_capturable_add(board, group_to, gi_from->lib[i]);
			else if (gi_to->libs == 1)  board_capturable_rm(board, group_to, gi_to->lib[0]);
#endif
			gi_to->lib[gi_to->libs++] = gi_from->lib[i];
			if (gi_to->libs >= GROUP_KEEP_LIBS)
				break;
next_from_lib:;
		}
	}

#ifdef FULL_BOARD
	board_pat3_fix(board, group_from, group_to);
#endif

	coord_t last_in_group;
	foreach_in_group(board, group_from) {
		last_in_group = c;
		group_at(board, c) = group_to;
	} foreach_in_group_end;

#ifdef BOARD_UNDO
	board_undo_t *u = board->u;
	u->merged[++u->nmerged_tmp].last = last_in_group;
#endif
	groupnext_at(board, last_in_group) = groupnext_at(board, group_base(group_to));
	groupnext_at(board, group_base(group_to)) = group_base(group_from);
	memset(gi_from, 0, sizeof(group_info_t));

	if (DEBUGL(7))  fprintf(stderr, "board_play_raw: merged group: %d\n", group_base(group_to));
}

static group_t profiling_noinline
new_group(board_t *board, coord_t coord)
{
	group_t group = coord;
	group_info_t *gi = &board_group_info(board, group);
	foreach_neighbor(board, coord, {
		if (board_at(board, c) == S_NONE)
			/* board_group_addlib is ridiculously expensive for us */
#if GROUP_KEEP_LIBS < 4
			if (gi->libs < GROUP_KEEP_LIBS)
#endif
			gi->lib[gi->libs++] = c;
	});

	group_at(board, coord) = group;
	groupnext_at(board, coord) = 0;

#ifdef FULL_BOARD
	if (gi->libs == 1)  board_capturable_add(board, group, gi->lib[0]);
#endif

	if (DEBUGL(8))
		fprintf(stderr, "new_group: added %d,%d to group %d\n",
			coord_x(coord), coord_y(coord), group_base(group));

	return group;
}

static inline group_t
play_one_neighbor(board_t *board,
		  coord_t coord, enum stone color, enum stone other_color,
		  coord_t c, group_t group)
{
	enum stone ncolor = board_at(board, c);
	group_t ngroup = group_at(board, c);

	inc_neighbor_count_at(board, c, color);

	if (!ngroup)  return group;

	board_group_rmlib(board, ngroup, coord);
	if (DEBUGL(7))  fprintf(stderr, "board_play_raw: reducing libs for group %d (%d:%d,%d)\n",
				group_base(ngroup), ncolor, color, other_color);

	if (ncolor == color && ngroup != group) {
		if (!group) {
			group = ngroup;
			add_to_group(board, group, c, coord);
		} else
			merge_groups(board, group, ngroup);
	} else if (ncolor == other_color) {
		if (DEBUGL(8)) {
			group_info_t *gi = &board_group_info(board, ngroup);
			fprintf(stderr, "testing captured group %d[%s]: ", group_base(ngroup), coord2sstr(group_base(ngroup)));
			for (int i = 0; i < GROUP_KEEP_LIBS; i++)
				fprintf(stderr, "%s ", coord2sstr(gi->lib[i]));
			fprintf(stderr, "\n");
		}
		if (unlikely(board_group_captured(board, ngroup)))
			board_group_capture(board, ngroup);
	}
	return group;
}

/* We played on a place with at least one liberty.
 * We will become a member of some group for sure. */
static group_t profiling_noinline
board_play_outside(board_t *board, move_t *m, int f)
{
	coord_t coord = m->coord;
	enum stone color = m->color;
	enum stone other_color = stone_other(color);
	group_t group = 0;

#ifdef BOARD_UNDO	
	undo_save_group_info(board, coord, color, board->u);
#endif
#ifdef FULL_BOARD	
	board_rmf(board, f);
#endif
	
	foreach_neighbor(board, coord, {
			group = play_one_neighbor(board, coord, color, other_color, c, group);
	});

	board_at(board, coord) = color;
	if (unlikely(!group))
		group = new_group(board, coord);

	board_commit_move(board, m);
#ifdef FULL_BOARD
	board_hash_update(board, coord, color);
	board_symmetry_update(board, &board->symmetry, coord);
#endif
	move_t ko = { pass, S_NONE };
	board->ko = ko;

	return group;
}

/* We played in an eye-like shape. Either we capture at least one of the eye
 * sides in the process of playing, or return -1. */
static int profiling_noinline
board_play_in_eye(board_t *board, move_t *m, int f)
{
	coord_t coord = m->coord;
	enum stone color = m->color;
	/* Check ko: Capture at a position of ko capture one move ago */
	if (unlikely(color == board->ko.color && coord == board->ko.coord)) {
		if (DEBUGL(5))
			fprintf(stderr, "board_check: ko at %d,%d color %d\n", coord_x(coord), coord_y(coord), color);
		return -1;
	} else if (DEBUGL(6))
		fprintf(stderr, "board_check: no ko at %d,%d,%d - ko is %d,%d,%d\n",
			color, coord_x(coord), coord_y(coord),
			board->ko.color, coord_x(board->ko.coord), coord_y(board->ko.coord));

	move_t ko = { pass, S_NONE };

	int captured_groups = 0;

	foreach_neighbor(board, coord, {
		group_t g = group_at(board, c);
		if (DEBUGL(7))
			fprintf(stderr, "board_check: group %d has %d libs\n",
				g, board_group_info(board, g).libs);
		captured_groups += (board_group_info(board, g).libs == 1);
	});

	if (likely(captured_groups == 0)) {
		if (DEBUGL(5)) {
			if (DEBUGL(6))  board_print(board, stderr);
			fprintf(stderr, "board_check: one-stone suicide\n");
		}
		return -1;
	}

#ifdef FULL_BOARD
	board_rmf(board, f);
#endif
#ifdef BOARD_UNDO
	undo_save_group_info(board, coord, color, board->u);
#endif

	int ko_caps = 0;
	coord_t cap_at = pass;
	foreach_neighbor(board, coord, {
		inc_neighbor_count_at(board, c, color);

		group_t group = group_at(board, c);
		if (!group)
			continue;

		board_group_rmlib(board, group, coord);
		if (DEBUGL(7))
			fprintf(stderr, "board_play_raw: reducing libs for group %d\n",
				group_base(group));

		if (board_group_captured(board, group)) {
			ko_caps += board_group_capture(board, group);
			cap_at = c;
		}
	});
	if (ko_caps == 1) {
		ko.color = stone_other(color);
		ko.coord = cap_at; // unique
		board->last_ko = ko;
		board->last_ko_age = board->moves + 1;  /* == board->moves really, board->moves++ done after */
		if (DEBUGL(5))
			fprintf(stderr, "guarding ko at %d,%s\n", ko.color, coord2sstr(ko.coord));
	}

	board_at(board, coord) = color;
	group_t group = new_group(board, coord);

	board_commit_move(board, m);
#ifdef FULL_BOARD
	board_hash_update(board, coord, color);
	board_hash_commit(board);
	board_symmetry_update(board, &board->symmetry, coord);
#endif
	board->ko = ko;

	return !!group;
}

static int __attribute__((flatten))
board_play_f(board_t *board, move_t *m, int f)
{
	if (DEBUGL(7))
		fprintf(stderr, "board_play(%s): ---- Playing %d,%d\n", coord2sstr(m->coord), coord_x(m->coord), coord_y(m->coord));
	if (likely(!board_is_eyelike(board, m->coord, stone_other(m->color)))) {
		/* NOT playing in an eye. Thus this move has to succeed. (This
		 * is thanks to New Zealand rules. Otherwise, multi-stone
		 * suicide might fail.) */
		group_t group = board_play_outside(board, m, f);
		if (unlikely(board_group_captured(board, group))) {
#ifdef BOARD_UNDO
			undo_save_suicide(board, m->coord, m->color, board->u);
#endif
			board_group_capture(board, group);
		}
#ifdef FULL_BOARD
		board_hash_commit(board);
#endif
		return 0;
	} else
		return board_play_in_eye(board, m, f);
}


static int
board_play_(board_t *board, move_t *m)
{
	assert(!is_resign(m->coord));  // XXX remove

	if (unlikely(is_pass(m->coord))) {
		board->passes[m->color]++;
		/* On pass, the player gives a pass stone to the opponent. */
		if (is_pass(m->coord) && board->rules == RULES_SIMING)
			board->captures[stone_other(m->color)]++;
		
		move_t nomove = { pass, S_NONE };
		board->ko = nomove;
		board_commit_move(board, m);
		return 0;
	}

	if (unlikely(board_at(board, m->coord) != S_NONE)) {
		if (DEBUGL(7)) fprintf(stderr, "board_check: stone exists\n");
		return -1;
	}

#ifdef FULL_BOARD
	int f = board->fmap[m->coord];
#endif
#ifdef BOARD_UNDO
	int f = -1;
#endif	
	return board_play_f(board, m, f);
}
