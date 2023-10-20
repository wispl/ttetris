#ifndef TETRIS_H
#define TETRIS_H

#include <stdbool.h>
#include <time.h>

/* grid dimensions */
#define MAX_ROW     20
#define MAX_COL     10

/* bag settings */
#define BAGSIZE     7
#define N_PREVIEW   5

#define LOCK_DELAY  0.5F

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
struct game {
	/* state of the grid, contains only placed pieces and emtpty cells */
	enum tetrimino_type grid[MAX_ROW][MAX_COL];
	bool has_lost;
	bool running;

	/* current piece */
	struct tetrimino tetrimino;

	/* absolute y-coordinate of the ghost piece 
	 * The ghost piece has the same rotation as the active piece
	 * just at the bottom of the stack
	 */
	int ghost_y;

	/* collected delta times used for updating physics */
	float accumulator;
	/* timestamp of previous frame */
	struct timespec time_prev;

	/* start of lock_delay */
	struct timespec lock_delay;
	/* whether active tetrimino is in autoplacement state */
	bool piece_lock;

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

	/* levels and scoring */
	int level;
	int lines_cleared;
	int score;
	int combo;
};

struct game *game_create();
void game_destroy(struct game *game);

void game_input(struct game *game);
void game_update(struct game *game);
void game_render(const struct game *game);
#endif /* TETRIS_H */