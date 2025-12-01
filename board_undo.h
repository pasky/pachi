#ifndef PACHI_BOARD_UNDO_H
#define PACHI_BOARD_UNDO_H

/* From uct/internal.h */
extern board_t *uct_main_board;

typedef struct {
	group_t	     group;
	coord_t	     last;  
	group_info_t info;
}  undo_merge_t;

typedef struct {
	group_t      group;
	group_info_t info;
	coord_t     *stones;
} undo_enemy_t;

typedef struct board_undo {
	move_t last_move2;
	move_t ko;
	move_t last_ko;
	int    last_ko_age;
	
	coord_t next_at;
	
	coord_t	inserted;
	undo_merge_t merged[4];
	int nmerged;
	int nmerged_tmp;

	int          nenemies;
	int          ncaptures;
	coord_t      *captures_end;
	undo_enemy_t enemies[4];
	coord_t      captures[BOARD_MAX_COORDS];
} board_undo_t;


/* Quick play/undo to try out a move.
 * WARNING  Only core board data structures are maintained !
 *          Code in between can't rely on anything else.
 *
 * Currently this means these can't be used:
 *   - incremental patterns (pat3)
 *   - hashes, superko_violation (spathash, hash, qhash, history_hash)
 *   - list of free positions (f / flen)
 *   - list of capturable groups (c / clen)
 *
 * #define QUICK_BOARD_CODE at the top of your file to get compile-time
 * error if you try to access a forbidden field.
 *
 * Invalid quick_play()/quick_undo() combinations (missing undo for example)
 * are caught at next board_play() if BOARD_UNDO_CHECKS is defined.
 */
int  board_quick_play(board_t *board, move_t *m, board_undo_t *u);
void board_quick_undo(board_t *b, move_t *m, board_undo_t *u);


/* UCT engine main board must be thread-safe.
 * Ensure with_move() doesn't get called on it so another thread
 * won't grab it in an invalid state. */
#ifdef EXTRA_CHECKS
#define WITH_MOVE_CHECKS(b)	assert((b) != uct_main_board)
#else
#define WITH_MOVE_CHECKS(b)
#endif

/* quick_play() + quick_undo() combo.
 * Body is executed only if move is valid (silently ignored otherwise).
 * Can break out in body, but definitely *NOT* return / jump around !
 * (caught at run time if BOARD_UNDO_CHECKS defined). Can use
 * with_move_return(val) to return value for non-nested with_move()'s
 * though. */
#define with_move(board, coord, color, body) \
       do { \
	       WITH_MOVE_CHECKS(board); \
	       board_t *board__ = (board);  /* For with_move_return() */ \
               move_t m_ = move((coord), (color)); \
               board_undo_t u_; \
               if (board_quick_play(board__, &m_, &u_) >= 0) { \
	               do {  body  } while(0); \
                       board_quick_undo(board__, &m_, &u_); \
	       } \
       } while (0)

/* Return value from within with_move() statement.
 * Valid for non-nested with_move() *ONLY* */
#define with_move_return(val)  \
	do { \
		typeof(val) val__ = (val); \
		board_quick_undo(board__, &m_, &u_); \
		return val__; \
	} while (0)

/* Same as with_move() but assert out in case of invalid move. */
#define with_move_strict(board, coord, color, body) \
       do { \
	       WITH_MOVE_CHECKS(board); \
	       board_t *board__ = (board);   /* For with_move_return() */ \
               move_t m_ = move((coord), (color)); \
               board_undo_t u_; \
               assert (board_quick_play(board__, &m_, &u_) >= 0); \
               do {  body  } while(0); \
               board_quick_undo(board__, &m_, &u_); \
       } while (0)


#endif /* PACHI_BOARD_UNDO_H */
