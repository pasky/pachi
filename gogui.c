#include <assert.h>
#include "board.h"
#include "engine.h"
#include "timeinfo.h"
#include "gtp.h"
#include "gogui.h"

static char *gogui_analyze_commands =
	"string/          Final Score/final_score\n"
	"gfx/gfx   Best Moves B/gogui-best_moves b\n"
	"gfx/gfx   Best Moves W/gogui-best_moves w\n"
	"gfx/gfx   Winrates B/gogui-winrates b\n"
	"gfx/gfx   Winrates W/gogui-winrates w\n"
	"gfx/gfx   Owner Map/gogui-owner_map\n"
	"gfx/Live gfx = Best Moves/gogui-live_gfx best_moves\n"
	"gfx/Live gfx = Best Sequence/gogui-live_gfx best_seq\n"
	"gfx/Live gfx = Winrates/gogui-live_gfx winrates\n";


char gogui_gfx_buf[5000];
enum gogui_reporting gogui_live_gfx = 0;

static void
gogui_set_live_gfx(struct engine *engine, char *arg)
{
	if (!strcmp(arg, "best_moves"))
		gogui_live_gfx = UR_GOGUI_CAN;
	if (!strcmp(arg, "best_seq"))
		gogui_live_gfx = UR_GOGUI_SEQ;
	if (!strcmp(arg, "winrates"))
		gogui_live_gfx = UR_GOGUI_WR;
	engine->live_gfx_hook(engine);
}

static char *
gogui_best_moves(struct engine *engine, struct board *b, struct time_info *ti, enum stone color,
		 bool winrates)
{
	assert(color != S_NONE);
	struct time_info *ti_genmove = time_info_genmove(b, ti, color);
	enum gogui_reporting prev = gogui_live_gfx;
	gogui_set_live_gfx(engine, (winrates ? "winrates" : "best_moves"));
	gogui_gfx_buf[0] = 0;
	engine->best_moves(engine, b, ti_genmove, color, NULL, NULL, 0);
	gogui_live_gfx = prev;
	return gogui_gfx_buf;
}

/* XXX Completely unsafe if reply buffer is not big enough */
static void
gogui_owner_map(struct board *b, struct engine *engine, char *reply)
{
	char str2[32];
	reply[0] = 0;
	if (!engine->owner_map)
		return;
	
	sprintf(reply, "INFLUENCE");
	foreach_point(b) {
		if (board_at(b, c) == S_OFFBOARD)
			continue;
		float p = engine->owner_map(engine, b, c);

		// p = -1 for WHITE, 1 for BLACK absolute ownership of point i
		if (p < -.8)
			p = -1.0;
		else if (p < -.5)
			p = -0.7;
		else if (p < -.2)
			p = -0.4;
		else if (p < 0.2)
			p = 0.0;
		else if (p < 0.5)
			p = 0.4;
		else if (p < 0.8)
			p = 0.7;
		else
			p = 1.0;
		sprintf(str2, " %3s %.1lf", coord2sstr(c, b), p);
		strcat(reply, str2);
	} foreach_point_end;

	strcat(reply, "\nTEXT Score Est: ");
	gtp_final_score_str(b, engine, str2, sizeof(str2));
	strcat(reply, str2);
}


enum parse_code
cmd_gogui_analyze_commands(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	gtp_reply(gtp, gogui_analyze_commands, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_live_gfx(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	gogui_set_live_gfx(engine, arg);
	return P_OK;
}

enum parse_code
cmd_gogui_owner_map(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char reply[5000];
	gogui_owner_map(board, engine, reply);
	gtp_reply(gtp, reply, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_best_moves(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	char *reply = gogui_best_moves(engine, board, ti, color, false);
	gtp_reply(gtp, reply, NULL);
	return P_OK;
}

enum parse_code
cmd_gogui_winrates(struct board *board, struct engine *engine, struct time_info *ti, gtp_t *gtp)
{
	char *arg;
	next_tok(arg);
	enum stone color = str2stone(arg);
	char *reply = gogui_best_moves(engine, board, ti, color, true);
	gtp_reply(gtp, reply, NULL);
	return P_OK;
}
