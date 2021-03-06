/*
 * Copyright (C) 2014  James Smith <james@theta.pw>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef BLOCKS_H_
#define BLOCKS_H_

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/queue.h>

#define PI 3.141592653589L

#define BLOCKS_MAX_COLUMNS	10
#define BLOCKS_MAX_ROWS		22

#define NUM_BLOCKS		7
#define NEXT_BLOCKS_LEN		5

#define LEN(x) ((sizeof(x))/(sizeof(*x)))

/* Define index yourself. We increment here at each definition */
#define PIECE_XY(X, Y) \
	block->p[index].x = (X); block->p[index].y = (Y); index++;

/* First, Second, and Third elements in the linked list */
#define HOLD_BLOCK() (pgame->blocks_head.lh_first)
#define CURRENT_BLOCK() (HOLD_BLOCK()->entries.le_next)
#define FIRST_NEXT_BLOCK() (CURRENT_BLOCK()->entries.le_next)

/* Does a block exist at the specified (y, x) coordinate? */
#define blocks_at_yx(y, x) (pgame->spaces[(y)] & (1 << (x)))


enum blocks_block_types {
	O_BLOCK,
	I_BLOCK,
	T_BLOCK,
	L_BLOCK,
	J_BLOCK,
	Z_BLOCK,
	S_BLOCK,
};

enum blocks_input_cmd {
	MOVE_LEFT,
	MOVE_RIGHT,
	MOVE_DOWN,			/* Move down one block */
	MOVE_DROP,			/* Drop block to bottom of board */
	ROT_LEFT,
	ROT_RIGHT,
	HOLD,
};

/* Only the currently falling block, the next block, and the save block are
 * stored in this structure. Once a block hits another piece, we forget about
 * it; it becomes part of the game board.
 */
struct blocks {
	uint32_t lock_delay;		/* how long to wait (nsec) */
	uint8_t soft_drop, hard_drop;	/* number of blocks dropped */
	uint8_t col_off, row_off;	/* column/row offsets */

	bool t_spin;			/* T-Spin . not implemented*/
	bool hold;			/* can only hold once */

	enum blocks_block_types type;

	struct pieces {			/* pieces stores two values(x, y) */
		int8_t x, y;		/* between -1 and +2 */
	} p[4];				/* each block has 4 pieces */

	LIST_ENTRY(blocks) entries;	/* LL entries */
};

struct blocks_game {
	/* These variables are read/written to the database
	 * when restoring/saving the game state
	 */
	uint16_t level, lines_destroyed;
	uint16_t spaces[BLOCKS_MAX_ROWS];	/* bit-field, one per row */
	uint32_t score;

	uint8_t *colors[BLOCKS_MAX_ROWS];	/* 1-to-1 with board */
	uint16_t pause_ticks;			/* total pause ticks per game */
	uint32_t nsec;				/* tick delay in nanoseconds */
	bool pause;				/* game pause */
	bool lose, quit;			/* how we quit */
	pthread_mutex_t lock;

	LIST_HEAD(blocks_head, blocks) blocks_head;	/* point to LL head */
};

extern struct blocks_game *pgame;

/* Create game state */
int blocks_init(void);

/* Free memory */
int blocks_cleanup(void);

/* Main loop, doesn't return until game is over */
void *blocks_loop(void *);

/* Input loop */
void *blocks_input(void *);

#endif				/* BLOCKS_H_ */
