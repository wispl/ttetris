#ifndef TETRIS_H
#define TETRIS_H

#include <stdbool.h>

/* grid dimensions */
#define MAX_ROW     20
#define MAX_COL     10

/* bag settings */
#define BAGSIZE     7
#define MAX_PREVIEW 5

#define LOCK_DELAY  0.5

enum tetrimino_type { EMPTY = -1, I, J, L, O, S, T, Z };
extern enum tetrimino_type type;

/* rotation mapping, indexed by [type][rotation][block][offset][x or y] */
extern const enum tetrimino_type ROTATIONS[7][4][4][2];

// TODO: integrate this later
/* enum rotation { */
/* 	up, right, down, left */
/* }; */

/* Representation of a tetrimino */
struct tetrimino {
	enum tetrimino_type type;
	int rotation;
	int y;
	int x;
};

/* Game structure, holds all revelant information related to a game */
typedef struct {
	int grid[MAX_ROW][MAX_COL];

	/* current piece */
	struct tetrimino tetrimino;

	/* collected delta times used for updating physics */
	float accumulator;
	/* start of lock_delay */
	float lock_delay;

	/* these bags are ring buffers and used for future pieces and preview */
	int bag_index;
	/* main bag used for preview */
	enum tetrimino_type bag[BAGSIZE];
	/* bag used for shuffling and replacing pieces in the main bag */
	enum tetrimino_type shuffle_bag[BAGSIZE];

	/* held piece */
	enum tetrimino_type hold;
	/* whether the player has held already */
	bool has_held;

	/* levels*/
	int level;
	int lines_cleared;
} game;

game *game_create();
void game_destroy(game *game);

/* Updates tetrimino according to gravity */
void game_update(game *game, float dt);

/**
 * @brief Gets the piece type at the given index
 *
 * @param index Index to preview, (between 0 and MAX_PREVIEW)
 * @return tetrimino_type of the preview
 */
enum tetrimino_type game_get_preview(game *game, int index);

/**
 * @brief Drops the tetrimino down as far as possible
 */
void game_harddrop_tetrimino(game *game);

/**
 * @brief Move the tetrimino with the given offsets if the movement is valid
 */
void game_move_tetrimino(game *game, int x_offset, int y_offset);

/**
 * @brief Hold the tetrimino, can only be used once per piece
 */
void game_hold_tetrimino(game *game);

/**
 * @brief Rotates the tetrimino \rotate_by times
 *
 * @param rotate_by Amount to rotate by, positive is clockwise, negative is
 * 	  	    counterclockwise
 */
void game_rotate_tetrimino(game *game, int rotate_by);

/**
 * @brief Get the y value of the ghost piece (preview of the current piece)
 *
 * @return y value of the ghost piece
 */
int game_ghost_y(game *game);

#endif /* TETRIS_H */
