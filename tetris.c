/**
 * @file
 * @brief Tetris game logic
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tetris.h"

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
 */
const int KICKTABLE[2][2][4][4][2] = {
	/* mappings for "J L S Z T" */
	/* these are organized so you can find the right rotation using
	 * the current rotation of the tetrimino */
	{ 
		/* counterclockwise */
		{{{ 1, 0}, { 1, -1}, {0,  2}, { 1,  2}},	// 0>>3
		 {{ 1, 0}, { 1,  1}, {0, -2}, { 1, -2}},	// 1>>0
		 {{-1, 0}, {-1, -1}, {0,  2}, {-1,  2}},	// 2>>1
		 {{-1, 0}, {-1,  1}, {0, -2}, {-1, -2}}},       // 3>>2 

		/* clockwise */
		{{{-1, 0}, {-1, -1}, {0,  2}, {-1,  2}},	// 0>>1
		 {{ 1, 0}, { 1,  1}, {0, -2}, { 1, -2}},	// 1>>2
		 {{ 1, 0}, { 1, -1}, {0,  2}, { 1,  2}},	// 2>>3
		 {{-1, 0}, {-1,  1}, {0, -2}, {-1, -2}}},  	// 3>>0
		
	},
	/* kicktable mapping for "I" */
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

/* TODO: add level based gravity */
const float gravity = 0.01667;

/**
 * @brief Shuffles the tetrimino bag in place (supports only BAGSIZE)
 */
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

/**
 * @brief Checks if a block is valid (in-bounds and on a free cell)
 *
 * @return true if valid, false if invalid
 */
static inline bool block_valid(int grid[MAX_ROW][MAX_COL], int x, int y)
{
	return x >= 0 && x < MAX_COL && y < MAX_ROW && grid[y][x] == EMPTY;
}

/**
 * @brief Checks if a row is filled (No EMPTY blocks)
 *
 * @return true if row is filled, false if not filled
 */
static bool row_filled(int grid[MAX_ROW][MAX_COL], int row)
{
	for (int n = 0; n < MAX_COL; ++n) {
		if (grid[row][n] == EMPTY) {
			return false;
		}
	}
	return true;
}

/**
 * @brief Checks if a tetrmino with the given rotation is valid at (x,y)
 *
 * @return true if rotation is invalid, false if invalid
 */
static bool rotation_valid(int grid[MAX_ROW][MAX_COL],
			   enum tetrimino_type type,
			   int rotation,
			   int x,
			   int y)
{
	for (int n = 0; n < 4; n++) {
		const int *offsets = ROTATIONS[type][rotation][n];
		if (!block_valid(grid, x + offsets[0], y + offsets[1])) {
			return false;
		}
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

/**
 * @brief Checks if the current held tetrimino and rotation is valid at the
 *        given offsets.
 *
 * @return true if valid, false if invalid
 */
static bool game_check_tetrimino(game *game, int x_offset, int y_offset)
{
	return rotation_valid(game->grid,
			      game->tetrimino.type,
			      game->tetrimino.rotation,
			      game->tetrimino.x + x_offset,
			      game->tetrimino.y + y_offset);
}

/**
 * @brief Gets the next tetrimino from the bag,
 * 	  the pieces are guranteed to be shuffled
 *
 * @return the tetrimino type of the next tetrimino
 */
static enum tetrimino_type game_next_tetrimino(game *game)
{
	/* store the piece, but replace its index with a shuffled piece */
	enum tetrimino_type type = game->bag[game->bag_index];
	game->bag[game->bag_index] = game->shuffle_bag[game->bag_index];

	/* exhausted both bags, shuffle the shuffle_bag */
	/* a 7-bag system is used, which means every piece is shuffled in a
	 * bag, guranteeing one piece of each type per bag */
	if (game->bag_index == BAGSIZE - 1) {
		bag_shuffle(game->shuffle_bag);
	}

	/* wrap around */
	game->bag_index = (game->bag_index + 1) % BAGSIZE;
	return type;
}

/**
 * @brief Spawns a new tetrimino piece with the given type into the grid
 */
static void spawn_tetrimino(game *game, enum tetrimino_type type)
{
	game->tetrimino.type = type;
	game->tetrimino.rotation = 0;
	game->tetrimino.y = 0;

	if (type == O) {
		game->tetrimino.x = 4;
	} else {
		game->tetrimino.x = 3;
	}
}

/**
 * @brief Clear filled rows and shift all rows down
 */
static void game_clear_rows(game *game, int row)
{
	int head = row;
	int tail = row;
	while (!row_filled(game->grid, head)) {
		--head;
		--tail;
	}

	while (head > 0) {
		if (row_filled(game->grid, head)) {
			clear_row(game->grid, head);
		} else {
			move_row(game->grid, head, tail);
			--tail;
		}
		--head;
	}
}

/**
 * @brief Place the current held tetrimino onto the grid
 */
static void game_place_tetrimino(game *game)
{
	bool any_rows_filled = false;
	int clear_begin = 0;

	for (int n = 0; n < 4; n++) {
		struct tetrimino tmino = game->tetrimino;
		const int *offsets = ROTATIONS[tmino.type][tmino.rotation][n];
		int x = tmino.x + offsets[0];
		int y = tmino.y + offsets[1];
		game->grid[y][x] = tmino.type;

		/* optimization: check if any rows are filled after placing,
		 * there is no need to check each frame for filled rows */
		if (row_filled(game->grid, y)) {
			any_rows_filled = true;
			if (y > clear_begin) {
				clear_begin = y;
			}
		}
	}

	if (any_rows_filled)
		game_clear_rows(game, clear_begin);

	/* get a new piece and allow the user to hold again */
	game->has_held = false;
	spawn_tetrimino(game, game_next_tetrimino(game));
}

void game_move_tetrimino(game *game, int x_offset, int y_offset)
{
	/* this is used for user controls so we check bounds */
	if (game_check_tetrimino(game, x_offset, y_offset)) {
		game->tetrimino.x += x_offset;
		game->tetrimino.y += y_offset;
	}
}

void game_rotate_tetrimino(game *game, int rotate_by)
{
	struct tetrimino tmino = game->tetrimino;
	/* wrap around 3, substitute for modulus when used on powers of 2 */
	int rotation = (game->tetrimino.rotation + rotate_by) & 3;
	if (rotation_valid(game->grid,
			   game->tetrimino.type,
			   rotation,
			   game->tetrimino.x,
			   game->tetrimino.y)) {
		game->tetrimino.rotation = rotation;
		return;
	}

	/* standard rotation failed, attempt kicktable rotations */
	int direction = rotate_by < 0 ? 0 : 1;
	bool is_I = tmino.type == I;
	for (int n = 0; n < 4; n++) {
		const int *offset =
			KICKTABLE[is_I][direction][tmino.rotation][n];
		if (rotation_valid(game->grid,
				   tmino.type,
				   rotation,
				   offset[0] + tmino.x,
				   offset[1] + tmino.y)) {
			/* once a rotation fits, use it and return */
			game->tetrimino.rotation = rotation;
			game->tetrimino.x += offset[0];
			game->tetrimino.y += offset[1];
			return;
		}
	}
}

int game_ghost_y(game *game)
{
	int y = 1;
	/* keep moving the current tetrimino down until it is invalid */
	while (game_check_tetrimino(game, 0, y)) {
		y++;
	}
	/* add the current tetrimino.y to convert from relative to absolute
	 * subtract one since the check failed after adding one. */
	return game->tetrimino.y + y - 1;
}

void game_harddrop_tetrimino(game *game)
{
	/* swap y's with the ghost piece, dropping it as far as possible */
	game->tetrimino.y = game_ghost_y(game);
	game_place_tetrimino(game);
}

enum tetrimino_type game_get_preview(game *game, int index)
{
	return game->bag[(game->bag_index + index) % BAGSIZE];
}

void game_tick(game *game, int frames)
{
	/* add gravity to g each frame */
	game->g += gravity * (float)frames;

	/* once g is grater than 1, apply gravity */
	if (game->g > 1.0F) {
		game->g = 0;

		if (game_check_tetrimino(game, 0, 1)) {
			game->tetrimino.y += 1;
		} else {
			/* TODO: Add lock delay */
			game_place_tetrimino(game);
		}
	}
}

void game_hold_tetrimino(game *game)
{
	/* user is not allow to hold twice in a row */
	if (game->has_held) {
		return;
	}
	game->has_held = true;

	/* swap hold and current piece */
	enum tetrimino_type hold = game->hold;
	game->hold = game->tetrimino.type;

	if (hold != EMPTY) {
		spawn_tetrimino(game, hold);
	} else {
		spawn_tetrimino(game, game_next_tetrimino(game));
	}
}

game *game_create()
{
	game *game = malloc(sizeof(*game));

	game->hold = EMPTY;
	game->has_held = false;
	game->g = 0.0F;
	game->bag_index = 0;
	for (int y = 0; y < MAX_ROW; ++y) {
		for (int x = 0; x < MAX_COL; ++x) {
			game->grid[y][x] = EMPTY;
		}
	}

	/* set initial state of the bag */
	enum tetrimino_type initial_bag[BAGSIZE] = {I, J, L, O, S, T, Z};
	memcpy(game->bag, initial_bag, sizeof(initial_bag));
	memcpy(game->shuffle_bag, initial_bag, sizeof(initial_bag));

	/* seed random and shuffle bags */
	srand(time(NULL));

	bag_shuffle(game->bag);
	bag_shuffle(game->shuffle_bag);

	spawn_tetrimino(game, game_next_tetrimino(game));
	return game;
}

void game_destroy(game *game)
{
	free(game);
}
