#include "tetris.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

 /* Tetris game logic */

const enum tetrimino_type ROTATIONS[7][4][4][2] = {
	[I] = {{{0, 1}, {1, 1}, {2, 1}, {3, 1}},
	       {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
	       {{3, 2}, {2, 2}, {1, 2}, {0, 2}},
	       {{1, 3}, {1, 2}, {1, 1}, {1, 0}}},

	[L] = {{{2, 0}, {2, 1}, {1, 1}, {0, 1}},
	       {{2, 2}, {1, 2}, {1, 1}, {1, 0}},
	       {{0, 2}, {0, 1}, {1, 1}, {2, 1}},
	       {{0, 0}, {1, 0}, {1, 1}, {1, 2}}},

	[O] = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}},
	       {{1, 0}, {1, 1}, {0, 1}, {0, 0}},
	       {{1, 1}, {0, 1}, {0, 0}, {1, 0}},
	       {{0, 1}, {0, 0}, {1, 0}, {1, 1}}},

	[Z] = {{{0, 0}, {1, 0}, {1, 1}, {2, 1}},
	       {{2, 0}, {2, 1}, {1, 1}, {1, 2}},
	       {{2, 2}, {1, 2}, {1, 1}, {0, 1}},
	       {{0, 2}, {0, 1}, {1, 1}, {1, 0}}},

	[T] = {{{1, 0}, {0, 1}, {1, 1}, {2, 1}},
	       {{2, 1}, {1, 0}, {1, 1}, {1, 2}},
	       {{1, 2}, {2, 1}, {1, 1}, {0, 1}},
	       {{0, 1}, {1, 2}, {1, 1}, {1, 0}}},

	[J] = {{{0, 0}, {0, 1}, {1, 1}, {2, 1}},
	       {{2, 0}, {1, 0}, {1, 1}, {1, 2}},
	       {{2, 2}, {2, 1}, {1, 1}, {0, 1}},
	       {{0, 2}, {1, 2}, {1, 1}, {1, 0}}},

	[S] = {{{2, 0}, {1, 0}, {1, 1}, {0, 1}},
	       {{2, 2}, {2, 1}, {1, 1}, {1, 0}},
	       {{0, 2}, {1, 2}, {1, 1}, {2, 1}},
	       {{0, 0}, {0, 1}, {1, 1}, {1, 2}}}};

/* kicktable mappings:
 * KICKTABLE[is_I piece][direction][rotation][tests][offsets]
 *
 * These are alternative rotations to try when the standard one fails.
 * There are 4 test cases done in order and choosing them depends on:
 * the rotation you are rotating from and the rotation you are rotating to,
 * notated as follows (from)>>(two)
 * 
 * These are organized so you can find the right rotation using
 * the current rotation of the tetrimino.
 */
const int KICKTABLE[2][2][4][4][2] = {
	/* mappings for "J L S Z T" */
	{ 
		/* counterclockwise */
		{{{ 1, 0}, { 1, -1}, {0,  2}, { 1,  2}},	// 0>>3
		 {{ 1, 0}, { 1,  1}, {0, -2}, { 1, -2}},	// 1>>0
		 {{-1, 0}, {-1, -1}, {0,  2}, {-1,  2}},	// 2>>1
		 {{-1, 0}, {-1,  1}, {0, -2}, {-1, -2}}},   // 3>>2 

		/* clockwise */
		{{{-1, 0}, {-1, -1}, {0,  2}, {-1,  2}},	// 0>>1
		 {{ 1, 0}, { 1,  1}, {0, -2}, { 1, -2}},	// 1>>2
		 {{ 1, 0}, { 1, -1}, {0,  2}, { 1,  2}},	// 2>>3
		 {{-1, 0}, {-1,  1}, {0, -2}, {-1, -2}}},  	// 3>>0
		
	},
	/* mapping for "I", "O" has no mapping */
	{
		/* counterclockwise */
		{{{-1, 0}, { 2, 0}, {-1, -2}, { 2,  1}}, 	// 0>>3
		 {{ 2, 0}, {-1, 0}, { 2, -1}, {-1,  2}}, 	// 1>>0
		 {{ 1, 0}, {-2, 0}, { 1,  2}, {-2, -1}}, 	// 2>>1
		 {{-2, 0}, { 1, 0}, {-2,  1}, { 1, -2}}},	// 3>>2 

		/* clockwise */
		{{{-2, 0}, { 1, 0}, {-2,  1}, { 1, -2}}, 	// 0>>1
		 {{-1, 0}, { 2, 0}, {-1, -2}, { 2,  1}}, 	// 1>>2
		 {{ 2, 0}, {-1, 0}, { 2, -1}, {-1,  2}}, 	// 2>>3
		 {{ 1, 0}, {-2, 0}, { 1,  2}, {-2, -1}}},	// 3>>0
		
	}};

/*** Time related functions ***/

/* returns the time it takes for a tetrimino to move down one row */
static inline float
get_gravity_speed(int level)
{
	/* TODO: Look into storing precomputed values */
	return powf(0.8 - ((level - 1) * 0.007), level - 1);
}

/* returns difference of time in seconds */
static inline float
diff_timespec(const struct timespec *t1, const struct timespec *t0)
{
	return (t1->tv_sec - t0->tv_sec) + (t1->tv_nsec - t0->tv_nsec) * 1e-9;
}

/* Shuffles the tetrimino bag in place (supports only BAGSIZE) */
static void bag_shuffle(enum tetrimino_type bag[BAGSIZE])
{
	int j, tmp;
	for (int i = BAGSIZE - 1; i > 0; --i) {
		j = rand() % (i + 1);
		tmp = bag[j];
		bag[j] = bag[i];
		bag[i] = tmp;
	}
}

/* Check if a block is valid (in-bounds and on a free cell) */
static inline bool block_valid(int grid[MAX_ROW][MAX_COL], int x, int y)
{
	/* even though rows above 0 is hidden, it is still valid */
	return x >= 0 && x < MAX_COL && y < MAX_ROW && (y < 0 || grid[y][x] == EMPTY);
}

/* Check if a row is filled (No EMPTY blocks) */
static bool row_filled(int grid[MAX_ROW][MAX_COL], int row)
{
	for (int n = 0; n < MAX_COL; ++n) {
		if (grid[row][n] == EMPTY)
			return false;
	}
	return true;
}

static void move_row(int grid[MAX_ROW][MAX_COL], int from, int to)
{
	for (int c = 0; c < MAX_COL; ++c) {
		grid[to][c] = grid[from][c];
		grid[from][c] = EMPTY;
	}
}

static void clear_row(int grid[MAX_ROW][MAX_COL], int row)
{
	for (int c = 0; c < MAX_COL; ++c)
		grid[row][c] = EMPTY;
}

/* Check if the active tetrimino is valid at the given rotation and offset */
static bool
game_tetrimino_valid(game *game, int rotation, int x_offset, int y_offset)
{
	for (int n = 0; n < 4; ++n) {
		const int *offsets = ROTATIONS[game->tetrimino.type][rotation][n];
		if (!block_valid(game->grid,
				 		 game->tetrimino.x + x_offset + offsets[0],
				 		 game->tetrimino.y + y_offset + offsets[1]))
			return false;
	}
	return true;
}

/* Gets the y-offset of the ghost piece, this is recalculated when the
 * coordinates of the active piece changes, so during rotation, movement, and
 * spawning a new piece */
static int get_ghost_offset(game *game)
{
	int y = 0;
	while (game_tetrimino_valid(game, game->tetrimino.rotation, 0, y + 1))
		++y;

	return y;
}

/* Get the next tetrimino from the bag and handles shuffling */
static enum tetrimino_type game_next_tetrimino(game *game)
{
	/* store the piece, but replace its index with a shuffled piece, this
	 * allows for previews near the end of the queue */
	enum tetrimino_type type = game->bag[game->bag_index];
	game->bag[game->bag_index] = game->shuffle_bag[game->bag_index];

	/* shuffle bag is exhausted, so shuffle it again */
	if (game->bag_index == BAGSIZE - 1)
		/* a 7-bag system requires shuffling all 7 types in a bag */
		bag_shuffle(game->shuffle_bag);

	/* increment and wrap around */
	game->bag_index = (game->bag_index + 1) % BAGSIZE;
	return type;
}

/* Spawn a new tetrimino piece with the given type into the grid */
static void spawn_tetrimino(game *game, enum tetrimino_type type)
{
	game->tetrimino.type = type;
	game->tetrimino.rotation = 0;

	game->tetrimino.y = -1;
	/* O-piece has a different starting placement */
	if (type == O)
		game->tetrimino.x = 4;
	else
		game->tetrimino.x = 3;
	game->ghost_y = get_ghost_offset(game) + game->tetrimino.y;

	/* reset time related settings for the next piece */
	game->accumulator = 0.0F;
	game->piece_lock = false;
}

/* Clear filled rows, and does book-keeping (points and row shifting)
 * To avoid shifting unchanged rows, only rows above the given row will be
 * checked and shifted */
static void game_clear_rows(game *game, int row)
{
	/* head will move up the array, removing filled rows and moving
	 * non-filled rows to the tail at the top of the stack */
	int head = row;
	int tail = row;
	int lines = 0;

	/* start at the first non-filled row */
	while (!row_filled(game->grid, head)) {
		--head;
		--tail;
	}

	while (head > 0) {
		if (row_filled(game->grid, head)) {
			clear_row(game->grid, head);
			++game->lines_cleared;
			++lines;

			/* new level every 10 levels */
			game->level = (game->lines_cleared / 10) + 1; 
		} else {
			move_row(game->grid, head, tail);
			--tail;
		}
		--head;
	}

	/* Scoring
	 * 1 -> 100 * level 
	 * 2 -> 300 * level 
	 * 3 -> 500 * level
	 * 4 -> 800 * level (also called a tetris)
	 */
	bool is_tetris = (lines == 4);
	game->score += (100 + (lines - 1) * 200 + (100 * is_tetris)) * game->level;
	
	int combo_extra = game->combo > 0 ? game->combo : 0;
	game->score += 50 * game->combo * combo_extra * game->level;
}

/* Place the active tetrimino and check for and handle line clears */
static void game_place_tetrimino(game *game)
{
	/* Check for line clears and get the lowest row which was cleared */
	bool rows_filled = false;
	int clear_begin = 0;

	for (int n = 0; n < 4; ++n) {
		enum tetrimino_type type = game->tetrimino.type;
		const int *offsets = ROTATIONS[type][game->tetrimino.rotation][n];
		int x = game->tetrimino.x + offsets[0];
		int y = game->tetrimino.y + offsets[1];

		if (y < 0) {
			game->has_lost = true;
			return;
		}
		game->grid[y][x] = type;

		/* A line clear can only be on the row of the four cells */
		if (row_filled(game->grid, y)) {
			rows_filled = true;
			if (y > clear_begin)
				clear_begin = y;
		}
	}

	if (rows_filled) {
		++game->combo;
		game_clear_rows(game, clear_begin);
	} else {
		game->combo = -1;
	}

	/* get a new piece and allow the user to hold again */
	game->has_held = false;
	spawn_tetrimino(game, game_next_tetrimino(game));
}

/*** Game controls ***/

void game_move_tetrimino(game *game, int x_offset, int y_offset)
{
	/* this is used for user controls so we check bounds */
	if (game_tetrimino_valid(game, game->tetrimino.rotation, x_offset, y_offset)) {
		game->tetrimino.x += x_offset;
		game->tetrimino.y += y_offset;
		game->ghost_y = get_ghost_offset(game) + game->tetrimino.y;

		/* get one point for each cell softdropped (move down) */
		if (y_offset > 0)
			++game->score;
	}
}

void game_rotate_tetrimino(game *game, int rotate_by)
{
	/* wrap around 3, substitute for modulus when used on powers of 2 */
	int rotation = (game->tetrimino.rotation + rotate_by) & 3;

	/* O-piece will alway pass because its rotation cannot failed */
	if (game_tetrimino_valid(game, rotation, 0, 0)) {
		game->tetrimino.rotation = rotation;
		game->ghost_y = get_ghost_offset(game) + game->tetrimino.y;
		return;
	}

	/* standard rotation failed, attempt kicktable rotations */
	int direction = rotate_by < 0 ? 0 : 1;
	bool is_I = game->tetrimino.type == I;
	for (int n = 0; n < 4; n++) {
		const int *offset = KICKTABLE[is_I][direction][game->tetrimino.rotation][n];

		if (game_tetrimino_valid(game, rotation, offset[0], offset[1])) {
			game->tetrimino.rotation = rotation;
			game->tetrimino.x += offset[0];
			game->tetrimino.y += offset[1];
			game->ghost_y = get_ghost_offset(game) + game->tetrimino.y;
			return;
		}
	}

	/* rotation failed, no change occurs */
}

void game_harddrop_tetrimino(game *game)
{
	/* add two points for each cell harddropped */
	game->score += get_ghost_offset(game) * 2;
	/* swap y's with the ghost piece, dropping it as far as possible */
	game->tetrimino.y = game->ghost_y;
	game_place_tetrimino(game);
}

void game_hold_tetrimino(game *game)
{
	/* user is not allow to hold twice in a row */
	if (game->has_held)
		return;

	game->has_held = true;

	/* swap hold and current piece */
	enum tetrimino_type hold = game->hold;
	game->hold = game->tetrimino.type;

	if (hold != EMPTY)
		spawn_tetrimino(game, hold);
	else
		spawn_tetrimino(game, game_next_tetrimino(game));
}

/*** Game states ***/

inline enum tetrimino_type game_get_preview(game *game, int index)
{
	return game->bag[(game->bag_index + index) % BAGSIZE];
}

void game_update(game *game)
{
	struct timespec time_now;
	clock_gettime(CLOCK_MONOTONIC, &time_now);

	/* delta time calculation */
	game->accumulator += diff_timespec(&time_now, &game->time_prev);
	game->time_prev = time_now;

	/* check if tetrimino can be moved down by gravity */
	if (game_tetrimino_valid(game, game->tetrimino.rotation, 0, 1)) {
		float gravity = get_gravity_speed(game->level);
		if (game->accumulator > gravity) {
			game->accumulator -= gravity;
			game->tetrimino.y += 1;
		}
	} else {
		/* start timer for autoplacement if tetrimino must be placed */
		if (!game->piece_lock) {
			game->piece_lock = true;
			game->lock_delay = time_now;
		}
	}

	/* piece autoplacement is independent of gravity */
	if (game->piece_lock 
			&& diff_timespec(&time_now, &game->lock_delay) > LOCK_DELAY) {
		game_place_tetrimino(game);
	}
}

game *game_create()
{
	game *game = malloc(sizeof(*game));

	game->hold = EMPTY;
	game->has_held = false;
	game->accumulator = 0.0F;
	game->piece_lock = false;
	game->bag_index = 0;
	game->has_lost = false;
	game->level = 1;
	game->lines_cleared = 0;
	game->score = 0;
	game->combo = -1;

	for (int y = 0; y < MAX_ROW; ++y) {
		for (int x = 0; x < MAX_COL; ++x)
			game->grid[y][x] = EMPTY;
	}

	/* set initial state of the bag */
	enum tetrimino_type initial_bag[BAGSIZE] = {I, J, L, O, S, T, Z};
	memcpy(game->bag, initial_bag, sizeof(initial_bag));
	memcpy(game->shuffle_bag, initial_bag, sizeof(initial_bag));

	/* seed random and shuffle bags */
	srand(time(NULL));

	bag_shuffle(game->bag);
	bag_shuffle(game->shuffle_bag);

	clock_gettime(CLOCK_MONOTONIC, &game->time_prev);
	spawn_tetrimino(game, game_next_tetrimino(game));
	return game;
}

void game_reset(game *game)
{
	game->hold = EMPTY;
	game->has_held = false;
	game->accumulator = 0.0F;
	game->piece_lock = false;
	game->bag_index = 0;
	game->has_lost = false;
	game->level = 1;
	game->lines_cleared = 0;
	game->score = 0;
	game->combo = -1;

	for (int y = 0; y < MAX_ROW; ++y) {
		for (int x = 0; x < MAX_COL; ++x)
			game->grid[y][x] = EMPTY;
	}

	bag_shuffle(game->bag);
	bag_shuffle(game->shuffle_bag);

	spawn_tetrimino(game, game_next_tetrimino(game));
}

void game_destroy(game *game)
{
	free(game);
}
