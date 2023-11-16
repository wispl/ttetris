#include "tetris.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <ncurses.h>
#elif _WIN32
#include <ncurses/ncurses.h>
#endif

/* grid dimensions */
#define EXTRA_ROWS  2
#define MAX_ROW     22
#define MAX_COL     10

/* bag settings */
#define BAGSIZE     7
#define N_PREVIEW   5
#define LOCK_DELAY  0.5F

#define BORDER_WIDTH  1
#define CELL_WIDTH    2

#define GRID_X        (COLS / 2) - (MAX_ROW / 2)
#define GRID_Y        (LINES - MAX_ROW) / 2
#define GRID_H        (MAX_ROW - 2) + 2 * BORDER_WIDTH
#define GRID_W        MAX_COL * CELL_WIDTH + BORDER_WIDTH * 2

/* space needed to place tetrmino within a box */
#define BOX_W         4 * CELL_WIDTH + 2 * BORDER_WIDTH
#define BOX_H         3 + 2 * BORDER_WIDTH

#define INFO_W        20
#define INFO_H        8

enum tetrimino_type { EMPTY = -1, I, J, L, O, S, T, Z };
enum window_type { GRID, PREVIEW, HOLD, INFO, NWINDOWS };

/* Representation of a tetrimino */
struct tetrimino {
	enum tetrimino_type type;
	int rotation;
	int y;
	int x;
};

/* Game structure, holds all revelant information related to a game */
struct game_state {
	/* state of the grid, contains only placed pieces and emtpty cells */
	enum tetrimino_type grid[MAX_ROW][MAX_COL];

	bool has_lost;
	bool running;

	/* current piece */
	struct tetrimino tetrimino;
	/* y-coordinate of the ghost piece */
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

static WINDOW* windows[NWINDOWS];
/* there should only be one instance of game_state */
static struct game_state game = {0};

/* rotation mapping, indexed by [type][rotation][block][offset][x or y] */
static const enum tetrimino_type ROTATIONS[7][4][4][2] = {
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
 * 1st test is for wallkicks (left and right)
 * 2nd test is for floorkicks
 * 3rd test is for right well kicks
 * 4th test is for left well kicks
 * 
 * These are alternative rotations to try when the standard one fails.
 * There are 4 test cases done in order and choosing them depends on:
 * the rotation you are rotating from and the rotation you are rotating to,
 * notated as follows (from)>>(two)
 * 
 * These are organized so you can find the right rotation using
 * the current rotation of the tetrimino.
 */
static const int KICKTABLE[2][2][4][4][2] = {
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

/* time for piece to drop based on level. Gravity is constant past level 20 */
static const float gravity_table[20] = {
	1.00000F, 0.79300F, 0.61780F, 0.47273F, 0.35520F, 0.26200F, 0.18968F,
	0.13473F, 0.09388F, 0.06415F, 0.04298F, 0.02822F, 0.01815F, 0.01144F,
	0.00706F, 0.00426F, 0.00252F, 0.00146F, 0.00082F, 0.00046F,
};

static inline float
diff_timespec(const struct timespec *t1, const struct timespec *t0)
{
	return (t1->tv_sec - t0->tv_sec) + (t1->tv_nsec - t0->tv_nsec) * 1e-9;
}

/*** Rendering ***/

/* chtype for rendering block of given tetrimino type */
static inline chtype
block_chtype(enum tetrimino_type type)
{
	return ' ' | A_REVERSE | COLOR_PAIR(type + 2);
}

/* renders a tetrimino of type with offset y, this renders *any* tetrimino */
static void
render_tetrimino(WINDOW *w, enum tetrimino_type type, int y_offset)
{
	for (int n = 0; n < 4; ++n) {
		const int *offset = ROTATIONS[type][0][n];
		int x = BORDER_WIDTH + (offset[0] * 2);
		int y = BORDER_WIDTH + offset[1] + y_offset;
		int c = block_chtype(type);

		mvwaddch(w, y, x, c);
		waddch(w, c);
	}
}

/* renders the active tetrimino as either a ghost or regular piece. */
static void
render_active_tetrimino(bool ghost)
{
	for (int n = 0; n < 4; ++n) {
		const int *offset = ROTATIONS[game.tetrimino.type][game.tetrimino.rotation][n];

		int x = (game.tetrimino.x + offset[0]) * 2;
		int y = (ghost ? game.ghost_y : game.tetrimino.y) + offset[1];
		int c = ghost ? '/' : block_chtype(game.tetrimino.type);

		if (y < 2)
			continue;

		mvwaddch(windows[GRID], BORDER_WIDTH + y - EXTRA_ROWS, BORDER_WIDTH + x, c);
		waddch(windows[GRID], c);
	}
}

static void
render_grid()
{
	for (int y = EXTRA_ROWS; y < MAX_ROW; ++y) {
		wmove(windows[GRID], BORDER_WIDTH + y - EXTRA_ROWS, BORDER_WIDTH);
		for (int x = 0; x < MAX_COL; ++x) {
			chtype c = block_chtype(game.grid[y][x]);
			waddch(windows[GRID], c);
			waddch(windows[GRID], c);
		}
	}

	/* Order is important, current piece should shadow the ghost piece */
	render_active_tetrimino(true);
	render_active_tetrimino(false);

	box(windows[GRID], 0, 0);
	wrefresh(windows[GRID]);
}

static void
render_preview()
{
	werase(windows[PREVIEW]);
	for (int p = 0; p < N_PREVIEW; ++p) {
		render_tetrimino(windows[PREVIEW], 
						 game.bag[(game.bag_index + p) % BAGSIZE], 
						 p * 3);
	}

	box(windows[PREVIEW], 0, 0);
	wrefresh(windows[PREVIEW]);
}

static void
render_hold()
{
	werase(windows[HOLD]);
	render_tetrimino(windows[HOLD], game.hold, 0);
	box(windows[HOLD], 0, 0);
	wrefresh(windows[HOLD]);
}

static void
render_info()
{
	werase(windows[INFO]);
	wprintw(windows[INFO], "Lines: %d\n" "Level: %d\n" "Score: %d\n" "Combo: %d\n",
							game.lines_cleared, game.level, game.score, game.combo);
	wrefresh(windows[INFO]);
}

static void
render_gameover()
{
    werase(windows[GRID]);
    mvwprintw(windows[GRID], 5, 5, "You lost!\n   Press R to restart");
    box(windows[GRID], 0, 0);
    wrefresh(windows[GRID]);
}

/*** Grid ***/

static inline bool
block_valid(int x, int y)
{
	return x >= 0 && x < MAX_COL && y >= 0 && y < MAX_ROW && game.grid[y][x] == EMPTY;
}

static bool
row_filled(int row)
{
	for (int n = 0; n < MAX_COL; ++n) {
		if (game.grid[row][n] == EMPTY)
			return false;
	}
	return true;
}

static bool
row_empty(int row)
{
	for (int n = 0; n < MAX_COL; ++n) {
		if (game.grid[row][n] != EMPTY)
			return false;
	}
	return true;
}

static void
move_row(int from, int to)
{
	for (int n = 0; n < MAX_COL; ++n) {
		game.grid[to][n] = game.grid[from][n];
		game.grid[from][n] = EMPTY;
	}
}

static void
clear_row(int row)
{
	for (int n = 0; n < MAX_COL; ++n)
		game.grid[row][n] = EMPTY;
}

/* Check if the tetrimino is valid at the given rotation and offset */
static bool
tetrimino_valid(struct tetrimino *tetrimino, int rotation, int x_offset, int y_offset)
{
	for (int n = 0; n < 4; ++n) {
		const int *offsets = ROTATIONS[tetrimino->type][rotation][n];
		if (!block_valid(tetrimino->x + x_offset + offsets[0],
				 		 tetrimino->y + y_offset + offsets[1]))
			return false;
	}
	return true;
}

/*** Game state ***/

/* Updates the ghost piece, recalculate when position of piece changes */
static void
update_ghost()
{
	int y = 0;
	while (tetrimino_valid(&game.tetrimino, game.tetrimino.rotation, 0, y + 1))
		++y;
	game.ghost_y = y + game.tetrimino.y;
}

static void
shuffle_bag(enum tetrimino_type bag[BAGSIZE])
{
	int j, tmp;
	for (int i = BAGSIZE - 1; i > 0; --i) {
		j = rand() % (i + 1);
		tmp = bag[j];
		bag[j] = bag[i];
		bag[i] = tmp;
	}
}

static enum tetrimino_type
next_tetrimino()
{
	/* replace with a piece from the shuffle bag to allow for previews */
	enum tetrimino_type type = game.bag[game.bag_index];
	game.bag[game.bag_index] = game.shuffle_bag[game.bag_index];

	game.bag_index = (game.bag_index + 1) % BAGSIZE;
	/* shuffle the shuffle_bag once it is exhausted */
	if (game.bag_index == 0)
		shuffle_bag(game.shuffle_bag);

	return type;
}

/* Spawn a new tetrimino piece with the given type onto the grid */
static void
spawn_tetrimino(enum tetrimino_type type)
{
	game.tetrimino.type = type;
	game.tetrimino.rotation = 0;

	/* O-piece has a different starting placement */
	game.tetrimino.x = (type == O) ? 4 : 3;
	game.tetrimino.y = 1;
	update_ghost();

	game.accumulator = 0.0F;
	game.piece_lock = false;
}

/* Clear filled rows, and does book-keeping (points and row shifting)
 * To avoid shifting unchanged rows, only rows above the given row will be
 * checked and shifted */
static void
clear_rows(int row)
{
	/* head will move up the array, removing filled rows and moving
	 * non-filled rows to the tail at the top of the stack */
	int head = row;
	int tail = row;
	int lines = 0;

	/* start at the first non-filled row */
	while (!row_filled(head)) {
		--head;
		--tail;
	}

	while (head > 0) {
		if (row_filled(head)) {
			clear_row(head);
			++game.lines_cleared;
			++lines;

			/* new level every 10 levels */
			game.level = (game.lines_cleared / 10) + 1; 
		} else {
			move_row(head, tail);
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
	game.score += (100 + (lines - 1) * 200 + (100 * is_tetris)) * game.level;
	
	int combo_extra = game.combo > 0 ? game.combo : 0;
	game.score += 50 * game.combo * combo_extra * game.level;

	/* perfect line clears */
	if (row_empty(MAX_ROW)) {
		int score = 0;
		switch (lines) {
		case 1: 
			score = 800;
			break;
		case 2:
			score = 1200;
			break;
		case 3:
			score = 1800;
			break;
		case 4:
			score = 2000;
			break;
		}
		game.score += score * game.level;
	}
}

/* Place the active tetrimino and check for and handle line clears */
static void
place_tetrimino()
{
	/* Check for line clears and get the lowest row which was cleared */
	bool rows_filled = false;
	int clear_begin = 0;

	for (int n = 0; n < 4; ++n) {
		enum tetrimino_type type = game.tetrimino.type;
		const int *offsets = ROTATIONS[type][game.tetrimino.rotation][n];
		int x = game.tetrimino.x + offsets[0];
		int y = game.tetrimino.y + offsets[1];

		if (y < 2) {
			game.has_lost = true;
			return;
		}
		game.grid[y][x] = type;

		/* A line clear can only be on the row of the four cells */
		if (row_filled(y)) {
			rows_filled = true;
			if (y > clear_begin)
				clear_begin = y;
		}
	}

	if (rows_filled) {
		++game.combo;
		clear_rows(clear_begin);
	} else {
		game.combo = -1;
	}

	/* get a new piece and allow the user to hold again */
	game.has_held = false;
	spawn_tetrimino(next_tetrimino(game));
}

/*** Game controls ***/

static void
controls_move(int x_offset, int y_offset)
{
	assert(y_offset >= 0 && "Tetrimino can not be moved up");
	if (tetrimino_valid(&game.tetrimino, game.tetrimino.rotation, x_offset, y_offset)) {
		game.tetrimino.x += x_offset;
		game.tetrimino.y += y_offset;
		game.score += y_offset;
		update_ghost();
	}
}

static void
controls_rotate(int rotate_by)
{
	/* wrap around 3, substitute for modulus when used on powers of 2 */
	int rotation = (game.tetrimino.rotation + rotate_by) & 3;
	if (tetrimino_valid(&game.tetrimino, rotation, 0, 0))
		goto success;

	/* standard rotation failed, attempt kicktable rotations */
	int direction = rotate_by < 0 ? 0 : 1;
	bool is_I = game.tetrimino.type == I;
	for (int n = 0; n < 4; n++) {
		const int *offset = KICKTABLE[is_I][direction][game.tetrimino.rotation][n];

		if (tetrimino_valid(&game.tetrimino, rotation, offset[0], offset[1])) {
			game.tetrimino.x += offset[0];
			game.tetrimino.y += offset[1];
			goto success;
		}
	}

	success: {
			game.tetrimino.rotation = rotation;
			update_ghost();
	}
}

static void
controls_harddrop()
{
	/* add two points for each cell harddropped */
	game.score += (game.ghost_y - game.tetrimino.y) * 2;
	game.tetrimino.y = game.ghost_y;
	place_tetrimino(game);
}

static void
controls_hold()
{
	if (game.has_held)
		return;

	game.has_held = true;
	enum tetrimino_type hold = game.hold;
	game.hold = game.tetrimino.type;

	if (hold != EMPTY)
		spawn_tetrimino(hold);
	else
		spawn_tetrimino(next_tetrimino(game));
}

/* reset game to its initial state and prepare for a new game */
static void
game_reset()
{
	game.running = true;
	game.hold = EMPTY;
	game.has_held = false;
	game.accumulator = 0.0F;
	game.piece_lock = false;
	game.bag_index = 0;
	game.has_lost = false;
	game.level = 1;
	game.lines_cleared = 0;
	game.score = 0;
	game.combo = -1;

	for (int y = 0; y < MAX_ROW; ++y) {
		for (int x = 0; x < MAX_COL; ++x)
			game.grid[y][x] = EMPTY;
	}

	shuffle_bag(game.bag);
	shuffle_bag(game.shuffle_bag);

	/* set previous time frame to prevent instant gravity on first frame */
	clock_gettime(CLOCK_MONOTONIC, &game.time_prev);
	spawn_tetrimino(next_tetrimino());
}

/*** Game loop ***/

void
game_input()
{
	switch (getch()) {
	case KEY_LEFT:
		controls_move(-1, 0);
		break;
	case KEY_RIGHT:
		controls_move(1, 0);
		break;
	case KEY_UP:
		controls_move(0, 1);
		break;
	case KEY_DOWN:
		controls_harddrop();
		break;
	case 'x':
		controls_rotate(1);
		break;
	case 'z':
		controls_rotate(-1);
		break;
	case 'c':
		controls_hold();
		render_hold();
		break;
	case 'r':
		game_reset();
		break;
	case 'q':
		game.running = false;
		break;
	default:
		return;
	}

	/* TODO: this is kind of random */
	render_info();
	render_preview();
}

void
game_update()
{
	struct timespec time_now;
	clock_gettime(CLOCK_MONOTONIC, &time_now);

	/* delta time calculation */
	game.accumulator += diff_timespec(&time_now, &game.time_prev);
	game.time_prev = time_now;

	/* check if tetrimino can be moved down by gravity */
	if (tetrimino_valid(&game.tetrimino, game.tetrimino.rotation, 0, 1)) {
		int i = game.level > 20 ? 20 : game.level - 1;
		if (game.accumulator > gravity_table[i]) {
			game.accumulator -= gravity_table[i];
			game.tetrimino.y += 1;
		}
	} else {
		/* start timer for autoplacement if tetrimino must be placed */
		if (!game.piece_lock) {
			game.piece_lock = true;
			game.lock_delay = time_now;
		}
	}

	/* piece autoplacement is independent of gravity */
	if (game.piece_lock && diff_timespec(&time_now, &game.lock_delay) > LOCK_DELAY)
		place_tetrimino();
}

void
game_render()
{
	if (game.has_lost)
		render_gameover();
	else 
		/* grid changes practically every frame, so always render it */
		render_grid();
}

bool
game_running()
{
	return game.running;
}

void
game_init()
{
	if (game.running) {
		fprintf(stderr, "ERROR: game and ncurses was initialized twice!");
		abort();
	}

    /* ncurses initialization */
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

	/* TODO: add check if terminal supports color */
	start_color();
	/* add 2 because color pairs start at 0 and the enums start from -1 */
	init_pair(EMPTY + 2, COLOR_BLACK, COLOR_BLACK);
	init_pair(I + 2, COLOR_CYAN, COLOR_BLACK);
	init_pair(J + 2, COLOR_BLUE, COLOR_BLACK);
	init_pair(L + 2, COLOR_WHITE, COLOR_BLACK);
	init_pair(O + 2, COLOR_YELLOW, COLOR_BLACK);
	init_pair(S + 2, COLOR_GREEN, COLOR_BLACK);
	init_pair(T + 2, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(Z + 2, COLOR_RED, COLOR_BLACK);

    windows[GRID] = newwin(GRID_H, GRID_W, GRID_Y, GRID_X);
    windows[HOLD] = newwin(BOX_H, BOX_W, GRID_Y, GRID_X - BOX_W - 5);
    windows[INFO] = newwin(INFO_H, INFO_W, LINES / 2, GRID_X - INFO_W);

    windows[PREVIEW] = newwin(N_PREVIEW * BOX_H, BOX_W, GRID_Y, GRID_X + GRID_W);

	/* seed random */
	srand(time(NULL));

	/* set initial state of bags */
	enum tetrimino_type initial_bag[BAGSIZE] = {I, J, L, O, S, T, Z};
	memcpy(game.bag, initial_bag, sizeof(initial_bag));
	memcpy(game.shuffle_bag, initial_bag, sizeof(initial_bag));

	game_reset();
}

void
game_destroy()
{
	wclear(stdscr);
	endwin();
}