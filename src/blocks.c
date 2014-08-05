#include <ctype.h>
#include <ncurses.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "db.h"
#include "blocks.h"
#include "debug.h"

extern WINDOW *board, *control;
extern const char *colors;
#define BLOCK_CHAR "x"

/* pointer to main game state */
struct block_game game, *pgame = &game;

/* three blocks, adjust block pointers to modify */
static struct block blocks[3];

/* Game difficulty */
enum block_diff {
	DIFF_EASY,
	DIFF_NORMAL,
	DIFF_HARD,
};

/* Movement commands */
enum block_cmd {
	MOVE_LEFT,		/* translate left */
	MOVE_RIGHT,		/* translate right */
	MOVE_COUNTER_CLOCKWISE,	/* rotate counter clockwise */
	MOVE_CLOCKWISE,		/* rotate clockwise */

	/* We don't actually use these .. */
	MOVE_DOWN,		/* Move piece one space down */
	MOVE_DROP,		/* Drop to the bottom of the board */
	MOVE_SAVE_PIECE,	/* save block for later */
};

#define NUM_BLOCKS 7
enum {
	SQUARE_BLOCK,
	LINE_BLOCK,
	T_BLOCK,
	L_BLOCK,
	L_REV_BLOCK,
	Z_BLOCK,
	Z_REV_BLOCK
};

/*
 * randomizes block and sets the initial positions of the pieces
 */
static void
random_block (struct block *block)
{
	if (!block)
		return;

	block->type = (uint8_t) rand () % NUM_BLOCKS;

	/* Try to center the block on the board */
	block->col_off = pgame->width/2;
	block->row_off = 1;
	block->color++;

	/* Each block contains four pieces.
	 * Each piece has a coordinate between [-1, 2] on each axes.
	 * The piece at (0, 0) is the pivot when we rotate.
	 */

	switch (block->type) {
	case SQUARE_BLOCK:
		block->p[0].x = -1;	block->p[0].y = -1;
		block->p[1].x =  0;	block->p[1].y = -1;
		block->p[2].x = -1;	block->p[2].y =  0;
		block->p[3].x =  0;	block->p[3].y =  0;
		break;
	case LINE_BLOCK:
		block->col_off--; // center
		block->p[0].x = -1;	block->p[0].y =  0;
		block->p[1].x =  0;	block->p[1].y =  0;
		block->p[2].x =  1;	block->p[2].y =  0;
		block->p[3].x =  2;	block->p[3].y =  0;
		break;
	case T_BLOCK:
		block->p[0].x =  0;	block->p[0].y = -1;
		block->p[1].x = -1;	block->p[1].y =  0;
		block->p[2].x =  0;	block->p[2].y =  0;
		block->p[3].x =  1;	block->p[3].y =  0;
		break;
	case L_BLOCK:
		block->p[0].x =  1;	block->p[0].y = -1;
		block->p[1].x = -1;	block->p[1].y =  0;
		block->p[2].x =  0;	block->p[2].y =  0;
		block->p[3].x =  1;	block->p[3].y =  0;
		break;
	case L_REV_BLOCK:
		block->p[0].x = -1;	block->p[0].y = -1;
		block->p[1].x = -1;	block->p[1].y =  0;
		block->p[2].x =  0;	block->p[2].y =  0;
		block->p[3].x =  1;	block->p[3].y =  0;
		break;
	case Z_BLOCK:
		block->p[0].x = -1;	block->p[0].y = -1;
		block->p[1].x =  0;	block->p[1].y = -1;
		block->p[2].x =  0;	block->p[2].y =  0;
		block->p[3].x =  1;	block->p[3].y =  0;
		break;
	case Z_REV_BLOCK:
		block->p[0].x =  0;	block->p[0].y = -1;
		block->p[1].x =  1;	block->p[1].y = -1;
		block->p[2].x = -1;	block->p[2].y =  0;
		block->p[3].x =  0;	block->p[3].y =  0;
		break;
	}
}

/*
 * Overwrite current block with next block. We then randomize the next block.
 */
static inline void
update_cur_next ()
{
	struct block *old = pgame->cur;
	pgame->cur = pgame->next;
	pgame->next = old;

	random_block (pgame->next);
}

/* Checks bounds on each piece in the block before writing it to the board */
static void
write_block (struct block *block)
{
	if (!block)
		return;

	int8_t px[4], py[4];

	for (size_t i = 0; i < LEN(pgame->cur->p); i++) {
		py[i] = block->row_off + block->p[i].y;
		px[i] = block->col_off + block->p[i].x;

		if (px[i] < 0 || px[i] >= pgame->width ||
		    py[i] < 0 || py[i] >= pgame->height)
			return;
	}

	for (size_t i = 0; i < LEN(pgame->cur->p); i++) {

		/* Set the bit where the block exists */
		pgame->spaces[py[i]] |= (1 << px[i]);
		pgame->colors[py[i]][px[i]] = block->color;
	}
}

/*
 * rotate pieces in blocks by either 90^ or -90^ around origin
 */
static int
rotate_block (struct block *block, enum block_cmd cmd)
{
	if (!block)
		return -1;

	if (block->type == SQUARE_BLOCK)
		return 1;

	int8_t mod = 1;
	if (cmd == MOVE_COUNTER_CLOCKWISE)
		mod = -1;

	/* Check each piece for a collision before we write any changes */
	for (size_t i = 0; i < LEN(block->p); i++) {

		int8_t bounds_x, bounds_y;
		bounds_x = block->p[i].y * (-mod) + block->col_off;
		bounds_y = block->p[i].x * ( mod) + block->row_off;

		/* Check out of bounds on each block */
		if (bounds_x < 0 || bounds_x >= pgame->width ||
		    bounds_y < 0 || bounds_y >= pgame->height ||
		    blocks_at_yx (bounds_y, bounds_x))
			return -1;
	}

	/* No collisions, so update the block position. */
	for (size_t i = 0; i < LEN(block->p); i++) {

		int8_t new_x, new_y;
		new_x = block->p[i].y * (-mod);
		new_y = block->p[i].x * ( mod);

		block->p[i].x = new_x;
		block->p[i].y = new_y;
	}

	return 1;
}

/*
 * translate pieces in block horizontally.
 */
static int
translate_block (struct block *block, enum block_cmd cmd)
{
	if (!block)
		return -1;

	int8_t dir = 1;
	if (cmd == MOVE_LEFT)
		dir = -1;

	/* Check each piece for a collision */
	for (size_t i = 0; i < LEN(block->p); i++) {

		int8_t bounds_x, bounds_y;
		bounds_x = block->p[i].x + block->col_off + dir;
		bounds_y = block->p[i].y + block->row_off;

		/* Check out of bounds before we write it */
		if (bounds_x < 0 || bounds_x >= pgame->width ||
		    bounds_y < 0 || bounds_y >= pgame->height ||
		    blocks_at_yx (bounds_y, bounds_x))
			return -1;
	}

	block->col_off += dir;

	return 1;
}

/* tries to advance the block one space down */
static int
drop_block (struct block *block)
{
	if (!block)
		return -1;

	for (uint8_t i = 0; i < LEN(block->p); i++) {

		int8_t bounds_y, bounds_x;
		bounds_y = block->p[i].y + block->row_off +1;
		bounds_x = block->p[i].x + block->col_off;

		if (bounds_y >= pgame->height ||
		    blocks_at_yx (bounds_y, bounds_x)) {
			return 0;
		}
	}

	block->row_off++;

	return 1;
}

static void
update_tick_speed ()
{
	/* tests/level-curve.c */
	double speed = 1;
	speed += atan(pgame->level/5.0) * 2/PI * (pgame->diff+1);

	pgame->nsec = (1E9/speed) -1;
}

static int
destroy_lines ()
{
	uint8_t destroyed = 0;

	/* If at any time the first two rows contain a block piece we lose. */
	for (int8_t i = 0; i < 2; i++) {
		for (int8_t j = 0; j < pgame->width; j++) {
			if (blocks_at_yx(i, j)) {
				pgame->lose = true;
			}
		}
	}

	/* Continue, even if we've lost, to tally points */

	/* Check each row for a full line of blocks */
	for (int8_t i = pgame->height-1; i >= 2; i--) {

		int8_t j = 0;
		for (; j < pgame->width; j++)
			if (blocks_at_yx (i, j) == 0)
				break;

		if (j != pgame->width)
			continue;

		debug ("Removed line %2d", i+1);
		/* bit field: setting to 0 removes line */
		pgame->spaces[i] = 0;

		destroyed++;

		for (int8_t k = i; k > 0; k--)
			pgame->spaces[k] = pgame->spaces[k-1];

		i++; // recheck this one
	}

	pgame->lines_destroyed += destroyed;

	/* We level up when we destroy (level*2 +5) lines. */
	if (pgame->lines_destroyed >= (pgame->level *2 + 5)) {

		/* level up, reset destroy count */
		pgame->level++;
		pgame->lines_destroyed = 0;

		update_tick_speed ();
	}

	/* If you destroy more than one line at a time then you can get a
	 * points boost. We add points *after* leveling up.
	 */
	pgame->score += destroyed * (pgame->level * (pgame->diff+1));

	return destroyed;
}

static void
draw_game (void)
{
	/* draw control screen */
	wattrset (control, COLOR_PAIR(2));
	mvwprintw (control, 0, 0, "Tetris-" VERSION);

	wattrset (control, COLOR_PAIR(3));
	mvwprintw (control, 2, 1, "%s", pgame->id);

	wattrset (control, COLOR_PAIR(1));
	mvwprintw (control, 3, 1, "Difficulty %d", pgame->diff);
	mvwprintw (control, 4, 1, "Level %d", pgame->level);
	mvwprintw (control, 5, 1, "Score %d", pgame->score);

	mvwprintw (control, 7, 1, "Next  Keep");
	mvwprintw (control, 8, 2, "          ");
	mvwprintw (control, 9, 2, "          ");

	mvwprintw (control, 11, 1, "Controls");

	mvwprintw (control, 13, 2, "Move [wasd]");
	mvwprintw (control, 14, 2, "Rotate [qe]");
	mvwprintw (control, 15, 2, "Keep [space]");
	mvwprintw (control, 16, 2, "Pause [p]");
	mvwprintw (control, 17, 2, "Quit [F3]");

	/* Draw current game pieces */
	for (size_t i = 0; i < LEN(pgame->next->p); i++) {
		char y, x;
		y = pgame->next->p[i].y;
		x = pgame->next->p[i].x;

		wattrset (control, COLOR_PAIR((pgame->next->color
				%LEN(colors)) +1) | A_BOLD);
		mvwprintw (control, y+9, x+3, BLOCK_CHAR);

	}

	for (size_t i = 0; pgame->save && i < LEN(pgame->save->p); i++) {
		char y, x;
		y = pgame->save->p[i].y;
		x = pgame->save->p[i].x;

		wattrset (control, COLOR_PAIR((pgame->save->color
				%LEN(colors)) +1) | A_BOLD);
		mvwprintw (control, y+9, x+9, BLOCK_CHAR);
	}

	wrefresh (control);

	/* game board */
	wattrset (board, A_BOLD | COLOR_PAIR(5));

	mvwvline (board, 0, 0, '*', pgame->height-1);
	mvwvline (board, 0, pgame->width+1, '*', pgame->height-1);
	mvwhline (board, pgame->height-2, 0, '*', pgame->width+2);

	/* Draw the game board, minus the two hidden rows above the game */
	for (uint8_t i = 2; i < pgame->height; i++) {
		wmove (board, i-2, 1);

		for (uint8_t j = 0; j < pgame->width; j++) {
			if (blocks_at_yx (i, j)) {
				wattrset (board, COLOR_PAIR((pgame->colors[i][j]
						%LEN(colors)) +1) | A_BOLD);
				wprintw (board, BLOCK_CHAR);
			} else {
				wattrset (board, COLOR_PAIR(1));
				wprintw (board, (j%2) ? "." : " ");
			}
		}
	}

	/* Draw the current block where it will land */
	struct block ghost;
	memcpy (&ghost, pgame->cur, sizeof *pgame->cur);
	while (drop_block (&ghost) == 1);

	wattrset (board, COLOR_PAIR(2) | A_DIM);
	for (size_t i = 0; i < LEN(ghost.p); i++) {
		int y, x;
		y = ghost.p[i].y + ghost.row_off;
		x = ghost.p[i].x + ghost.col_off;

		mvwprintw (board, y-2, x+1, BLOCK_CHAR);
	}

	/* Draw the current block as it's falling */
	wattrset (board, COLOR_PAIR(pgame->cur->color %LEN(colors) +1)|A_BOLD);
	for (size_t i = 0; i < LEN(pgame->cur->p); i++) {
		int y, x;
		y = pgame->cur->p[i].y + pgame->cur->row_off;
		x = pgame->cur->p[i].x + pgame->cur->col_off;

		mvwprintw (board, y-2, x+1, BLOCK_CHAR);
	}

	/* Overlay the PAUSED text */
	wattrset (board, COLOR_PAIR(1) | A_BOLD);
	if (pgame->pause) {
		int y, x;

		/* Center the text horizontally, place the text slightly above
		 * the middle vertically.
		 */
		x = (pgame->width  -6)/2 +1;
		y = (pgame->height -2)/2 -2;

		mvwprintw (board, y, x, "PAUSED");
	}

	wrefresh (board);
}

int
blocks_init (void)
{
	log_info ("Initializing game data");
	memset (pgame, 0, sizeof *pgame);

	pthread_mutex_init (&pgame->lock, NULL);

	/* Default values  */
	pgame->width = 10;
	pgame->height = (pgame->width *2) +2;
	pgame->diff = DIFF_NORMAL;
	pgame->level = 1;

	strncpy(pgame->id, "No Name", sizeof pgame->id);

	pgame->nsec = 1E9 -1; // nanosleep() fails if nsec is >= 1E9

	/* randomize the initial blocks */
	for (size_t i = 0; i < LEN(blocks); i++) {
		random_block (&blocks[i]);
		/* Start with random color, so cur and next don't follow each
		 * other. Then just increment normally. */
		blocks[i].color = rand();
	}

	pgame->cur = &blocks[0];
	pgame->next = &blocks[1];
	pgame->save = NULL;

	/* Allocate the maximum size necessary to accomodate the
	 * largest board size */
	for (uint8_t i = 0; i < BLOCKS_ROWS; i++) {
		pgame->colors[i] = calloc (BLOCKS_COLUMNS,
				sizeof (*pgame->colors[i]));
		if (!pgame->colors[i]) {
			log_err ("Out of memory");
			exit (2);
		}
	}

	return 1;
}

int
blocks_clean (void)
{
	log_info ("Cleaning game data");
	pthread_mutex_destroy (&pgame->lock);

	/* Free all the memory allocated */
	for (uint8_t i = 0; i < BLOCKS_ROWS; i++)
		free (pgame->colors[i]);

	memset (pgame, 0, sizeof *pgame);

	return 1;
}

void *
blocks_input (void *not_used)
{
	(void) not_used;

	int ch;
	while ((ch = getch())) {

		/* prevent modification of the game from blocks_main
		 * in the other thread */
		pthread_mutex_lock (&pgame->lock);

		switch (toupper(ch)) {
		case 'A':
			translate_block (pgame->cur, MOVE_LEFT);
			break;
		case 'D':
			translate_block (pgame->cur, MOVE_RIGHT);
			break;
		case 'S':
			drop_block (pgame->cur);
			break;
		case 'W':
			/* drop the block to the bottom of the game */
			while (drop_block (pgame->cur) == 1);
			break;
		case 'Q':
			rotate_block (pgame->cur,
				MOVE_COUNTER_CLOCKWISE);
			break;
		case 'E':
			rotate_block (pgame->cur,
				MOVE_CLOCKWISE);
			break;
		case ' ':
			/* Swap next and save pieces */
			if (pgame->save == NULL) {
				pgame->save = &blocks[2];
			}
			struct block *tmp = pgame->save;
			pgame->save = pgame->next;
			pgame->next = tmp;
			break;
		case 'P':
			pgame->pause = !pgame->pause;
			break;
		case KEY_F(3):
			pgame->pause = false;
			pgame->quit = true;
			break;
		}

		draw_game ();

		pthread_mutex_unlock (&pgame->lock);
	}

	return NULL;
}

int
blocks_main (void)
{
	wclear (control);
	wclear (board);
	draw_game ();

	pthread_t input;
	pthread_create (&input, NULL, blocks_input, NULL);

	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 0;

	/* The database sets the current level for the game.
	 * Update the tick delay so we resume at proper difficulty
	 */
	update_tick_speed ();

	for (;;) {
		ts.tv_nsec = pgame->nsec;
		nanosleep (&ts, NULL);

		if (pgame->pause)
			continue;

		if (pgame->lose || pgame->quit)
			break;

		pthread_mutex_lock (&pgame->lock);

		if (drop_block (pgame->cur) == 0) {
			write_block (pgame->cur);
			/* save the game when we remove a line */
			if (destroy_lines ())
				db_save_state ();

			update_cur_next ();
		}

		draw_game ();

		pthread_mutex_unlock (&pgame->lock);
	}

	pthread_cancel (input);

	if (pgame->lose)
		db_save_score ();
	else
		db_save_state ();

	return 1;
}
