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

#define BORDER_WIDTH  1
#define CELL_WIDTH    2

/* Grid, MAX_ROW & MAX_COL are for arrays*/
#define MAX_ROW       22
#define MAX_COL       10
#define GRID_X        (COLS / 2) - (MAX_ROW / 2)
#define GRID_Y        (LINES - MAX_ROW) / 2
#define GRID_H        (MAX_ROW - 2) + 2 * BORDER_WIDTH
#define GRID_W        MAX_COL * CELL_WIDTH + BORDER_WIDTH * 2
#define EXTRA_ROWS    2
/* A box with enough space to fit any tetrimino */
#define BOX_W         4 * CELL_WIDTH + 2 * BORDER_WIDTH
#define BOX_H         3 + 2 * BORDER_WIDTH
/* Stats section */
#define STATS_W       20
#define STATS_H       8

/* game configuration */
#define BAGSIZE     		7
#define NPREVIEW   			5
#define LOCK_DELAY  		0.5F
#define ACTION_TEXT_EXPIRE  2.0F

/* Action mapping of (enum, text, and points) */
#define FOR_EACH_ACTION(X) \
	X(NONE, , 0) \
	X(SINGLE, SINGLE, 100) \
	X(DOUBLE, DOUBLE, 300) \
	X(TRIPLE, TRIPLE, 500) \
	X(TETRIS, TETRIS, 800) \
	X(PERFECT_SINGLE, PERFECT SINGLE, 800) \
	X(PERFECT_DOUBLE, PERFECT DOUBLE, 1200) \
	X(PERFECT_TRIPLE, PERFECT TRIPLE, 1800) \
	X(PERFECT_TETRIS, PERFECT TETRIS, 2000) \
	X(MINI_TSPIN, MINI T-SPIN, 100) \
	X(MINI_TSPIN_SINGLE, MINI T-SPIN SINGLE, 200) \
	X(MINI_TSPIN_DOUBLE,  MINI T-SPIN DOUBLE, 400) \
	X(TSPIN, T-SPIN, 400) \
	X(TSPIN_SINGLE, T-SPIN SINGLE, 800) \
	X(TSPIN_DOUBLE, T-SPIN DOUBLE, 1200) \
	X(TSPIN_TRIPLE, T-SPIN TRIPLE, 1600) \

#define GENERATE_ENUM(ENUM, TEXT, POINTS) ENUM,
#define GENERATE_TEXT(ENUM, TEXT, POINTS) #TEXT,
#define GENERATE_POINTS(ENUM, TEXT, POINTS) POINTS,

enum action_type    { FOR_EACH_ACTION(GENERATE_ENUM) };
enum tetrimino_type { EMPTY = -1, I, J, L, O, S, T, Z };
enum window_type    { GRID, PREVIEW, HOLD, STATS, ACTION, NWINDOWS };

static const int ACTION_POINTS[] = { FOR_EACH_ACTION(GENERATE_POINTS) };
static const char* ACTION_TEXT[] = { FOR_EACH_ACTION(GENERATE_TEXT) };

/* Game structure, holds all revelant information related to a game */
struct game_state {
	bool has_lost;
	bool running;

	/* state of the grid, contains only placed pieces and emtpty cells */
	enum tetrimino_type grid[MAX_ROW][MAX_COL];
	/* current piece */
	struct tetrimino {
		enum tetrimino_type type;
		int rotation;
		int x, y;
	} tetrimino;

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
	int move_reset;

	/* start of when action text is dispalyed */
	struct timespec action_start;

	/* these bags are ring buffers and used for pieces and preview */
	int bag_index;
	enum tetrimino_type bag[BAGSIZE];
	enum tetrimino_type shuffle_bag[BAGSIZE];

	/* held piece */
	enum tetrimino_type hold;
	/* whether the player has held already */
	bool has_held;

	int level;
	int lines_cleared;
	int score;
	int high_score;
	int combo;

	enum action_type tspin;
	bool back_to_back;
};

static WINDOW* windows[NWINDOWS];
static struct game_state game = {0};

/* rotation mapping, indexed by [type][rotation][block][x or y] */
static const int ROTATIONS[7][4][4][2] = {
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
	       {{0, 0}, {0, 1}, {1, 1}, {1, 2}}}
};

/* KICKTABLE[is_I piece][direction][rotation][tests][offsets]
 *
 * Tests are in order from:
 * wallkicks (left and right), floorkicks, right well kicks, left well kicks
 *
 * These are alternative rotations for when the natural one fails and is chosen
 * based on: the current rotation and the desired rotation (from)>>(to)
 *
 * These are organized so the right rotation can is indexed using the
 * the current rotation of the tetrimino.
 */
static const int KICKTABLE[2][2][4][4][2] = {
	/* tests for "J L S Z T" */
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
	/* tests for "I" */
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
	}
};

/* Time for piece to drop based on level. Gravity is constant past level 20 */
static const float gravity_table[20] = {
	1.00000F, 0.79300F, 0.61780F, 0.47273F, 0.35520F, 0.26200F, 0.18968F,
	0.13473F, 0.09388F, 0.06415F, 0.04298F, 0.02822F, 0.01815F, 0.01144F,
	0.00706F, 0.00426F, 0.00252F, 0.00146F, 0.00082F, 0.00046F,
};

/* Coordinates for block n with given rotation for the current tetrimino */
static inline int
block_x(int rotation, int n)
{
	return game.tetrimino.x + ROTATIONS[game.tetrimino.type][rotation][n][0];
}

static inline int
block_y(int rotation, int n)
{
	return game.tetrimino.y + ROTATIONS[game.tetrimino.type][rotation][n][1];
}

/* Actions which can maintain a back-to-back */
static bool
is_difficult(enum action_type type)
{
	switch (type) {
	case TETRIS:
	case MINI_TSPIN_SINGLE:
	case MINI_TSPIN_DOUBLE:
	case TSPIN_SINGLE:
	case TSPIN_DOUBLE:
	case TSPIN_TRIPLE:
	case PERFECT_TETRIS:		
		return true;
	default:
		return false;
	}
	return false;
}

static inline float
diff_timespec(const struct timespec *t1, const struct timespec *t0)
{
	return (t1->tv_sec - t0->tv_sec) + (t1->tv_nsec - t0->tv_nsec) * 1e-9;
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
		int x = 2 * block_x(game.tetrimino.rotation, n);
		int y = block_y(game.tetrimino.rotation, n);
		/* use game.ghost_y instead of game.tetrimino.y for ghost pieces*/
		y += (ghost * (game.ghost_y - game.tetrimino.y));
		chtype c = ghost ? '/' : block_chtype(game.tetrimino.type);

		if (y < EXTRA_ROWS) 
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
	for (int p = 0; p < NPREVIEW; ++p) {
		enum tetrimino_type type = game.bag[(game.bag_index + p) % BAGSIZE];
		render_tetrimino(windows[PREVIEW], type, p * 3);
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
render_stats()
{
	werase(windows[STATS]);
	wprintw(windows[STATS],
			"Lines: %d\n"
			"Level: %d\n"
			"Score: %d\n"
			"High Score: %d\n"
			"Combo: %d\n",
			game.lines_cleared,
			game.level,
			game.score,
			game.high_score,
			game.combo);
	wrefresh(windows[STATS]);
}

static void
render_announce(enum action_type type, bool back_to_back)
{
	clock_gettime(CLOCK_MONOTONIC, &game.action_start);

    werase(windows[ACTION]);
	int pad = (GRID_W - strlen(ACTION_TEXT[type])) / 2;
    wprintw(windows[ACTION], "%*s%s", pad, "", ACTION_TEXT[type]);
    mvwprintw(windows[ACTION], 1, 5, "%s", (back_to_back) ? "BACK TO BACK" : "");
    wrefresh(windows[ACTION]);
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

/* Check if the current tetrimino is valid at the given rotation and offset */
static bool
tetrimino_valid(int rotation, int x_offset, int y_offset)
{
	for (int n = 0; n < 4; ++n) {
		int x = block_x(rotation, n) + x_offset;
		int y = block_y(rotation, n) + y_offset;
		if (!block_valid(x, y))
			return false;
	}
	return true;
}

static void
check_tspin(int kick_test)
{
	/* list of corners, clockwise from base rotation, a sliding window is used 
	 * to get the correct corners based on rotation.
	 */
	static const int corners[4][2] = { {0, 0}, {2, 0}, {2, 2}, {0, 2} };
	/* filled corners: front-left, front-right, back-right, back-left */
	bool filled[4];

	for (int i = 0; i < 4; ++i) {
		int index = (game.tetrimino.rotation + i) & 3;
		filled[i] = !block_valid(corners[index][0] + game.tetrimino.x,
								 corners[index][1] + game.tetrimino.y);
	}

	if (filled[0] && filled[1] && (filled[2] || filled[3])) {
		game.tspin = TSPIN;
	} else if (filled[2] && filled[3] && (filled[0] || filled[1])) {
		game.tspin = (kick_test == 3) ? TSPIN : MINI_TSPIN;
	} else {
		game.tspin = NONE;
		return;
	}

	render_announce(game.tspin, false);
}

/*** Game state ***/

/* Updates the ghost piece, recalculate when position of piece changes */
static void
update_ghost()
{
	int y = 0;
	while (tetrimino_valid(game.tetrimino.rotation, 0, y + 1))
		++y;
	game.ghost_y = y + game.tetrimino.y;
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
	game.move_reset = 0;
	game.tspin = NONE;
}

/* updates score and levels after line clears */
static void
update_score(int lines)
{
	/* where there are no tspin, game.tspin is NONE or 0 */
	enum action_type action = lines + game.tspin;
	bool back_to_back = is_difficult(action) && game.back_to_back;
	double score = ACTION_POINTS[action] * ((back_to_back) ? 1.5 : 1);

	/* combo bonuses */
	score += 50 * (game.combo < 0 ? 0 : game.combo);

	/* perfect line clear bonuses are added to regular clear bonuses */
	if (row_empty(MAX_ROW - 1))
		score += (back_to_back) ? 3200 : ACTION_POINTS[PERFECT_SINGLE + lines];

	game.score += (int) (score * game.level);
	render_announce(action, back_to_back);

	/* new level every 10 line clears */
	game.lines_cleared += lines;
	game.level = (game.lines_cleared / 10) + 1;

	game.combo = (lines == 0) ? -1 : game.combo + 1;
	/* these are the only actions which can break the chain */
	game.back_to_back = (action != NONE && action != SINGLE && action != SINGLE
			     		 && action != DOUBLE && action != TRIPLE);
}

/* Clear filled rows and shifts rows down. Returns lines cleared */
static int
update_rows(int row)
{
	/* head will move up the array, removing filled rows and moving
	 * non-filled rows to the tail at the top of the stack */
	int head = row;
	int tail = row;
	int lines = 0;

	while (head > 0) {
		if (row_filled(head)) {
			clear_row(head);
			++lines;
		} else {
			move_row(head, tail);
			--tail;
		}
		--head;
	}

	return lines;
}

/* Place the active tetrimino and handle line clears */
static void
place_tetrimino()
{
	int clear_begin = -1;
	for (int n = 0; n < 4; ++n) {
		int x = block_x(game.tetrimino.rotation, n);
		int y = block_y(game.tetrimino.rotation, n);
		game.grid[y][x] = game.tetrimino.type;
		clear_begin = (row_filled(y) && y > clear_begin) ? y : clear_begin;
	}

	int lines = (clear_begin != -1) ? update_rows(clear_begin) : 0;
	update_score(lines);

	/* check for overflow only after lines have been cleared */ 
	if (!row_empty(1)) {
		game.has_lost = true;
		game.high_score = (game.score > game.high_score) ? game.score : game.high_score;
		return;
	}

	game.has_held = false;
	spawn_tetrimino(next_tetrimino(game));
}

/*** Game controls ***/

static void
controls_move(int x_offset, int y_offset)
{
	assert(y_offset >= 0 && "Tetrimino can not be moved up");
	if (tetrimino_valid(game.tetrimino.rotation, x_offset, y_offset)) {
		game.tetrimino.x += x_offset;
		game.tetrimino.y += y_offset;
		game.score += y_offset;
		update_ghost();

		if (x_offset != 0 && game.move_reset < 15) {
			++game.move_reset;
			game.piece_lock = false;
		}
	}
}

static void
controls_rotate(int rotate_by)
{
	int rotation = (game.tetrimino.rotation + rotate_by) & 3;
	int kick_test = 0;

	/* perform natural rotation */
	if (tetrimino_valid(rotation, 0, 0))
		goto success;

	/* natural rotation failed, attempt kicktable rotations */
	int direction = rotate_by < 0 ? 0 : 1;
	bool is_I = game.tetrimino.type == I;
	for (int n = 0; n < 4; ++n) {
		const int *offset = KICKTABLE[is_I][direction][game.tetrimino.rotation][n];

		if (tetrimino_valid(rotation, offset[0], offset[1])) {
			game.tetrimino.x += offset[0];
			game.tetrimino.y += offset[1];
			kick_test = n;
			goto success;
		}
	}

	return;
	success: {
			game.tetrimino.rotation = rotation;
			update_ghost();

			if (game.tetrimino.type == T)
				check_tspin(kick_test);

			if (game.move_reset < 15) {
				++game.move_reset;
				game.piece_lock = false;
			}
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
reset_game()
{
	int highscore = game.high_score;
	game = (struct game_state) {0};
	game.running = true;
	game.has_held = false;
	game.has_lost = false;
	game.back_to_back = false;
	game.piece_lock = false;
	game.hold = EMPTY;
	game.tspin = NONE;
	game.level = 1;
	game.combo = -1;
	game.high_score = highscore;

	for (int y = 0; y < MAX_ROW; ++y) {
		for (int x = 0; x < MAX_COL; ++x)
			game.grid[y][x] = EMPTY;
	}

	enum tetrimino_type initial_bag[BAGSIZE] = {I, J, L, O, S, T, Z};
	memcpy(game.bag, initial_bag, sizeof(initial_bag));
	memcpy(game.shuffle_bag, initial_bag, sizeof(initial_bag));

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
	int key = getch();
	if (!game.has_lost) {
		switch (key) {
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
			break;
		case 'q':
			game.running = false;
			break;
		default:
			break;
		}
	}
	if (key == 'r')
		reset_game();
}

void
game_update()
{
	struct timespec time_now;
	clock_gettime(CLOCK_MONOTONIC, &time_now);

	game.accumulator += diff_timespec(&time_now, &game.time_prev);
	game.time_prev = time_now;

	/* check if tetrimino can be moved down by gravity */
	if (tetrimino_valid(game.tetrimino.rotation, 0, 1)) {
		int i = game.level > 20 ? 19 : game.level - 1;
		if (game.accumulator > gravity_table[i]) {
			game.accumulator -= gravity_table[i];
			game.tetrimino.y += 1;
		}
	} else {
		/* start autoplacement */
		if (!game.piece_lock) {
			game.piece_lock = true;
			game.lock_delay = time_now;
		}
	}

	/* piece autoplacement is independent of gravity */
	if (game.piece_lock && diff_timespec(&time_now, &game.lock_delay) > LOCK_DELAY)
		place_tetrimino();

	if (diff_timespec(&time_now, &game.action_start) > ACTION_TEXT_EXPIRE) {
		werase(windows[ACTION]);
		wrefresh(windows[ACTION]);
	}
}

void
game_render()
{
	if (game.has_lost) {
		render_gameover();
	} else {
		render_grid();
		render_hold();
		render_stats();
		render_preview();
	}
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
		fprintf(stderr, "ERROR: ncurses was initialized twice!");
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

    windows[GRID]    = newwin(GRID_H, GRID_W, GRID_Y, GRID_X);
    windows[HOLD]    = newwin(BOX_H, BOX_W, GRID_Y, GRID_X - BOX_W - 5);
    windows[STATS]   = newwin(STATS_H, STATS_W, LINES / 2, GRID_X - STATS_W);
    windows[ACTION]  = newwin(2, GRID_W, GRID_Y + GRID_H, GRID_X);
    windows[PREVIEW] = newwin(NPREVIEW * BOX_H, BOX_W, GRID_Y, GRID_X + GRID_W);

	srand(time(NULL));
	reset_game();
}

void
game_destroy()
{
	wclear(stdscr);
	endwin();
}