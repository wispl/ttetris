#include "tetris.h"
#include "extern/miniaudio.h"

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

#define CELL_WIDTH    2
#define BORDER_OFFSET 1
#define BORDERS       2

/* grid placement and dimensions, other UIs are based on these */
#define HIDDEN_ROWS   2
#define GRID_ROWS     (20 + HIDDEN_ROWS)
#define GRID_COLS     10
#define GRID_H        (GRID_ROWS - HIDDEN_ROWS + BORDERS)
#define GRID_W        ((GRID_COLS * CELL_WIDTH) + BORDERS)
#define GRID_X        ((COLS  - GRID_W) / 2)
#define GRID_Y        ((LINES - GRID_H) / 2)

/* game configuration */
#define BAGSIZE     	    7
#define NPREVIEW   	    5
#define LOCK_DELAY  	    0.5F
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

struct game_state {
	bool running, has_lost;

	int score, high_score;
	int level, lines_cleared; /* new level every 10 line clears */
	int combo;                /* consecutive clears counter */
	bool back_to_back;        /* difficult line clear bonuses */
	enum action_type tspin;   /* tspin bonuses: NONE, MINI_TSPIN or TSPIN */

	float accumulator;	      /* accumulated delta times */
	struct timespec time_prev;    /* previous frame for delta time*/
	struct timespec action_start; /* use to expire the action text */

	bool piece_lock;            /* autoplacement of piece due to gravity */
	struct timespec lock_delay; /* start of lock delay for autoplacement */
	int move_reset; 	    /* piece_lock can be reset upto 15 times */

	enum tetrimino_type grid[GRID_ROWS][GRID_COLS];
	struct tetrimino {
		enum tetrimino_type type;
		int rotation;
		int x, y;
		int ghost_y; /* preview of the tetrimino at the bottom */
	} tetrimino;         /* currently held tetrimino */

	int bag_index;
	enum tetrimino_type bag[BAGSIZE]; 	  /* preview and queue */
	enum tetrimino_type shuffle_bag[BAGSIZE]; /* 7-bag shuffle system */

	enum tetrimino_type hold; /* held piece */
	bool has_held;            /* hold could only be used once per piece */
};

static WINDOW* windows[NWINDOWS]; /* ncurses windows */
static struct game_state game = {0};
static ma_decoder audio_decoder;
static ma_device audio_device;

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
 * The tests are alternative rotations when the natural one fails and are
 * chosen based on: the current rotation and the desired rotation (from)>>(to)
 *
 * These are organized so the right rotation can be indexed using the
 * the current rotation of the tetrimino.
 */
static const int KICKTABLE[2][2][4][4][2] = {
	/* tests for "J L S Z T" */
	{
		/* counterclockwise */
		{{{ 1, 0}, { 1, -1}, {0,  2}, { 1,  2}},  // 0>>3
		 {{ 1, 0}, { 1,  1}, {0, -2}, { 1, -2}},  // 1>>0
		 {{-1, 0}, {-1, -1}, {0,  2}, {-1,  2}},  // 2>>1
		 {{-1, 0}, {-1,  1}, {0, -2}, {-1, -2}}}, // 3>>2
		/* clockwise */
		{{{-1, 0}, {-1, -1}, {0,  2}, {-1,  2}},  // 0>>1
		 {{ 1, 0}, { 1,  1}, {0, -2}, { 1, -2}},  // 1>>2
		 {{ 1, 0}, { 1, -1}, {0,  2}, { 1,  2}},  // 2>>3
		 {{-1, 0}, {-1,  1}, {0, -2}, {-1, -2}}}, // 3>>0
	},
	/* tests for "I" */
	{
		/* counterclockwise */
		{{{-1, 0}, { 2, 0}, {-1, -2}, { 2,  1}},  // 0>>3
		 {{ 2, 0}, {-1, 0}, { 2, -1}, {-1,  2}},  // 1>>0
		 {{ 1, 0}, {-2, 0}, { 1,  2}, {-2, -1}},  // 2>>1
		 {{-2, 0}, { 1, 0}, {-2,  1}, { 1, -2}}}, // 3>>2
		/* clockwise */
		{{{-2, 0}, { 1, 0}, {-2,  1}, { 1, -2}},  // 0>>1
		 {{-1, 0}, { 2, 0}, {-1, -2}, { 2,  1}},  // 1>>2
		 {{ 2, 0}, {-1, 0}, { 2, -1}, {-1,  2}},  // 2>>3
		 {{ 1, 0}, {-2, 0}, { 1,  2}, {-2, -1}}}, // 3>>0
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
block_x(int rot, int n)
{
	return game.tetrimino.x + ROTATIONS[game.tetrimino.type][rot][n][0];
}

static inline int
block_y(int rot, int n)
{
	return game.tetrimino.y + ROTATIONS[game.tetrimino.type][rot][n][1];
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
	return ' ' | COLOR_PAIR(type + 1);
}

/* renders a tetrimino of type with offset y, this renders *any* tetrimino */
static void
render_tetrimino(WINDOW *w, enum tetrimino_type type, int y_offset)
{
	for (int n = 0; n < 4; ++n) {
		const int *offset = ROTATIONS[type][0][n];
		int x = BORDER_OFFSET + (offset[0] * CELL_WIDTH);
		int y = BORDER_OFFSET + offset[1] + y_offset;
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
		int x = block_x(game.tetrimino.rotation, n) * CELL_WIDTH;
		int y = block_y(game.tetrimino.rotation, n);
		/* use game.ghost_y instead for ghost pieces */
		y += (ghost * (game.tetrimino.ghost_y - game.tetrimino.y));
		chtype c = ghost ? '/' : block_chtype(game.tetrimino.type);

		if (y >= HIDDEN_ROWS) {
			int row = BORDER_OFFSET + y - HIDDEN_ROWS;
			int col = BORDER_OFFSET + x;
			mvwaddch(windows[GRID], row, col, c);
			waddch(windows[GRID], c);
		}
	}
}

static void
render_grid()
{
	for (int y = HIDDEN_ROWS; y < GRID_ROWS; ++y) {
		int row = BORDER_OFFSET + y - HIDDEN_ROWS;
		wmove(windows[GRID], row, BORDER_OFFSET);
		for (int x = 0; x < GRID_COLS; ++x) {
			chtype c = block_chtype(game.grid[y][x]);
			waddch(windows[GRID], c);
			waddch(windows[GRID], c);
		}
	}
	/* Actual tetrimino should cover the ghost preview */
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
		int index = (game.bag_index + p) % BAGSIZE;
		enum tetrimino_type type = game.bag[index];
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
	mvwprintw(windows[ACTION],
	   	  1,
	   	  5,
	   	  "%s",
	   	  (back_to_back) ? "BACK TO BACK" : "");
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
	return x >= 0 && x < GRID_COLS && y >= 0 && y < GRID_ROWS && game.grid[y][x] == EMPTY;
}

static bool
row_filled(int row)
{
	for (int n = 0; n < GRID_COLS; ++n) {
		if (game.grid[row][n] == EMPTY)
			return false;
	}
	return true;
}

static bool
row_empty(int row)
{
	for (int n = 0; n < GRID_COLS; ++n) {
		if (game.grid[row][n] != EMPTY)
			return false;
	}
	return true;
}

static void
move_row(int from, int to)
{
	for (int n = 0; n < GRID_COLS; ++n) {
		game.grid[to][n] = game.grid[from][n];
		game.grid[from][n] = EMPTY;
	}
}

static void
clear_row(int row)
{
	for (int n = 0; n < GRID_COLS; ++n)
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
	/* list of corners clockwise, starting index is current rotation */
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
	game.tetrimino.ghost_y = y + game.tetrimino.y;
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
	if (row_empty(GRID_ROWS - 1))
		score += (back_to_back) ? 3200 : ACTION_POINTS[PERFECT_SINGLE + lines];

	game.score += (int) (score * game.level);
	render_announce(action, back_to_back);

	game.lines_cleared += lines;
	game.level = (game.lines_cleared / 10) + 1; /* new level every 10 lines */
	game.combo = (lines == 0) ? -1 : game.combo + 1;
	/* t-spins and mini-tspins do not break the chain */
	if (!back_to_back && game.back_to_back)
		game.back_to_back = (action == TSPIN || action == MINI_TSPIN);
	else
		game.back_to_back = is_difficult(action);
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
		if (game.score > game.high_score)
			game.high_score = game.score;
		return;
	}

	game.has_held = false;
	spawn_tetrimino(next_tetrimino());
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

		if (game.piece_lock && ++game.move_reset < 15)
			game.piece_lock = false;
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

		if (game.piece_lock && ++game.move_reset < 15)
			game.piece_lock = false;
	}
}

static void
controls_harddrop()
{
	/* add two points for each cell harddropped */
	game.score += (game.tetrimino.ghost_y - game.tetrimino.y) * 2;
	game.tetrimino.y = game.tetrimino.ghost_y;
	place_tetrimino();
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
		spawn_tetrimino(next_tetrimino());
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

	for (int y = 0; y < GRID_ROWS; ++y) {
		for (int x = 0; x < GRID_COLS; ++x)
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
	if (game.has_lost && key == 'r') {
		reset_game();
		return;
	}
	switch (key) {
	case KEY_LEFT: 	controls_move(-1, 0); 	break;
	case KEY_RIGHT: controls_move(1, 0); 	break;
	case KEY_UP: 	controls_move(0, 1); 	break;
	case KEY_DOWN: 	controls_harddrop(); 	break;
	case 'x': 	controls_rotate(1); 	break;
	case 'z': 	controls_rotate(-1); 	break;
	case 'c': 	controls_hold(); 	break;
	case 'r': 	reset_game(); 		break;
	case 'q': 	game.running = false; 	break;
	default: break;
	}
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

/* callback needed for miniaudio */
static void
data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame)
{
	ma_decoder* decoder = device->pUserData;
	if (decoder == NULL)
		return;
	
	ma_data_source_read_pcm_frames(decoder, output, frame, NULL);
	(void) input;
}

int
game_init()
{
	if (game.running)
		return -1;

	/* ncurses initialization */
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);

	if (has_colors()) {
		start_color();
		use_default_colors();
		/* add 1 because color pairs start at 0 */
		init_pair(I + 1, -1, COLOR_CYAN);
		init_pair(J + 1, -1, COLOR_BLUE);
		init_pair(L + 1, -1, COLOR_WHITE);
		init_pair(O + 1, -1, COLOR_YELLOW);
		init_pair(S + 1, -1, COLOR_GREEN);
		init_pair(T + 1, -1, COLOR_MAGENTA);
		init_pair(Z + 1, -1, COLOR_RED);
	} /* no colors then */

	/* Enough space to fit any tetrimino with borders */
	int box_w   = (4 * CELL_WIDTH) + BORDERS;
	int box_h   = 3 + BORDERS;
	/* don't multiply the borders width, add it afterwards */
	int preview_h = (NPREVIEW * (box_h - BORDERS)) + BORDERS;

	windows[GRID]    = newwin(GRID_H, GRID_W, GRID_Y, GRID_X);
	windows[HOLD]    = newwin(box_h, box_w, GRID_Y, GRID_X - box_w);
	windows[STATS]   = newwin(8, 20, LINES / 2, GRID_X - 20);
	windows[ACTION]  = newwin(2, GRID_W, GRID_Y + GRID_H, GRID_X);
	windows[PREVIEW] = newwin(preview_h, box_w, GRID_Y, GRID_X + GRID_W);

	/* audio and sound initialization */
	if (ma_decoder_init_file("assets/tetris.mp3", NULL, &audio_decoder) != MA_SUCCESS)
		return -1;

	ma_data_source_set_looping(&audio_decoder, MA_TRUE);

	ma_device_config device_config;
	device_config = ma_device_config_init(ma_device_type_playback);
	device_config.noPreSilencedOutputBuffer = true;
	device_config.playback.format   = audio_decoder.outputFormat;
	device_config.playback.channels = audio_decoder.outputChannels;
	device_config.sampleRate        = audio_decoder.outputSampleRate;
	device_config.dataCallback      = data_callback;
	device_config.pUserData         = &audio_decoder;
	device_config.noClip            = true;

	if (ma_device_init(NULL, &device_config, &audio_device) != MA_SUCCESS) {
		ma_decoder_uninit(&audio_decoder);
		return -1;
	}

	if (ma_device_start(&audio_device) != MA_SUCCESS) {
		ma_device_uninit(&audio_device);
		ma_decoder_uninit(&audio_decoder);
		return -1;
	}

	srand(time(NULL));
	reset_game();
	return 1;
}

void
game_destroy()
{
	ma_device_uninit(&audio_device);
	ma_decoder_uninit(&audio_decoder);
	wclear(stdscr);
	endwin();
}
