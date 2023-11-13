#include "tetris.h"

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

#define GRID_X        (COLS / 2) - (MAX_ROW / 2)
#define GRID_Y        (LINES - MAX_ROW) / 2
#define GRID_H        MAX_ROW + 2 * BORDER_WIDTH
#define GRID_W        MAX_COL * CELL_WIDTH + BORDER_WIDTH * 2

/* space needed to place tetrmino within a box */
#define BOX_W         4 * CELL_WIDTH + 2 * BORDER_WIDTH
#define BOX_H         3 + 2 * BORDER_WIDTH

#define INFO_W        20
#define INFO_H        8

enum window_type { GRID, PREVIEW, HOLD, INFO, NWINDOWS };
static WINDOW* windows[NWINDOWS];

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

/* time for piece to drop based on level. 
 * After level 20, the gravity is constant*/
static const float gravity_table[20] = {
	1.00000F, 0.79300F, 0.61780F, 0.47273F, 0.35520F, 0.26200F, 0.18968F,
	0.13473F, 0.09388F, 0.06415F, 0.04298F, 0.02822F, 0.01815F, 0.01144F,
	0.00706F, 0.00426F, 0.00252F, 0.00146F, 0.00082F, 0.00046F,
};

/* returns difference of time in seconds */
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

/* renders the active tetrimino as either a ghost piece or regular piece. This
 * duplication is due to cells not being rendered above row 20 */
static void
render_active_tetrimino(const struct game *game, bool ghost)
{
	for (int n = 0; n < 4; ++n) {
		const int *offset = ROTATIONS[game->tetrimino.type][game->tetrimino.rotation][n];

		int x = (game->tetrimino.x + offset[0]) * 2;
		int y = (ghost ? game->ghost_y : game->tetrimino.y) + offset[1];
		int c = ghost ? '/' : block_chtype(game->tetrimino.type);

		/* do not render rows above 0 */
		if (y < 0)
			continue;

		mvwaddch(windows[GRID], BORDER_WIDTH + y, BORDER_WIDTH + x, c);
		waddch(windows[GRID], c);
	}
}

static void
render_grid(const struct game *game)
{
	for (int y = 0; y < MAX_ROW; ++y) {
		wmove(windows[GRID], 1 + y, 1);
		for (int x = 0; x < MAX_COL; ++x) {
			chtype c = block_chtype(game->grid[y][x]);
			waddch(windows[GRID], c);
			waddch(windows[GRID], c);
		}
	}

	/* Order is important, current piece should shadow the ghost piece */
	/* Ghost piece*/
	render_active_tetrimino(game, true);
	/* Current piece */
	render_active_tetrimino(game, false);

	box(windows[GRID], 0, 0);
	wrefresh(windows[GRID]);
}

static void
render_preview(const struct game *game)
{
	werase(windows[PREVIEW]);
	for (int p = 0; p < N_PREVIEW; ++p) {
		enum tetrimino_type type = game->bag[(game->bag_index + p) % BAGSIZE];
		render_tetrimino(windows[PREVIEW], type, p * 3);
	}

	box(windows[PREVIEW], 0, 0);
	wrefresh(windows[PREVIEW]);
}

static void
render_hold(const struct game *game)
{
	werase(windows[HOLD]);
	render_tetrimino(windows[HOLD], game->hold, 0);
	box(windows[HOLD], 0, 0);
	wrefresh(windows[HOLD]);
}

static void
render_info(const struct game *game)
{
	werase(windows[INFO]);
	mvwprintw(windows[INFO], 1, 1, "Lines: %d", game->lines_cleared);
	mvwprintw(windows[INFO], 2, 1, "Level: %d", game->level);
	mvwprintw(windows[INFO], 3, 1, "Score: %d", game->score);
	mvwprintw(windows[INFO], 4, 1, "Combo: %d", game->combo);
	wrefresh(windows[INFO]);
}

static void
render_gameover(const struct game* game)
{
    werase(windows[GRID]);
    mvwprintw(windows[GRID], 5, 5, "You lost!\n Press R to restart");
    box(windows[GRID], 0, 0);
    wrefresh(windows[GRID]);
}

/*** Grid manipulation and checks ***/

/* TODO: Look into passing in game* insead of just grid */
/* Check if a block is valid (in-bounds and on a free cell) */
static inline bool
block_valid(enum tetrimino_type grid[MAX_ROW][MAX_COL], int x, int y)
{
	/* even though rows above 0 is hidden, it is still valid */
	return x >= 0 && x < MAX_COL && y < MAX_ROW && (y < 0 || grid[y][x] == EMPTY);
}

static bool
row_filled(enum tetrimino_type grid[MAX_ROW][MAX_COL], int row)
{
	for (int n = 0; n < MAX_COL; ++n) {
		if (grid[row][n] == EMPTY)
			return false;
	}
	return true;
}

static bool
row_empty(enum tetrimino_type grid[MAX_ROW][MAX_COL], int row)
{
	for (int n = 0; n < MAX_COL; ++n) {
		if (grid[row][n] != EMPTY)
			return false;
	}
	return true;
}

static void
move_row(enum tetrimino_type grid[MAX_ROW][MAX_COL], int from, int to)
{
	for (int c = 0; c < MAX_COL; ++c) {
		grid[to][c] = grid[from][c];
		grid[from][c] = EMPTY;
	}
}

static void
clear_row(enum tetrimino_type grid[MAX_ROW][MAX_COL], int row)
{
	for (int c = 0; c < MAX_COL; ++c)
		grid[row][c] = EMPTY;
}

/* Check if the active tetrimino is valid at the given rotation and offset */
static bool
game_tetrimino_valid(const struct game *game, int rotation, int x_offset, int y_offset)
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

/*** Game state manipulation ***/

/* Gets the y-offset of the ghost piece, this is recalculated when the
 * coordinates of the active piece changes, so during rotation, movement, and
 * spawning a new piece */
static int
get_ghost_offset(const struct game *game)
{
	int y = 0;
	while (game_tetrimino_valid(game, game->tetrimino.rotation, 0, y + 1))
		++y;

	return y;
}

/* Shuffles the tetrimino bag in place (supports only BAGSIZE) */
static void
bag_shuffle(enum tetrimino_type bag[BAGSIZE])
{
	int j, tmp;
	for (int i = BAGSIZE - 1; i > 0; --i) {
		j = rand() % (i + 1);
		tmp = bag[j];
		bag[j] = bag[i];
		bag[i] = tmp;
	}
}

/* Get the next tetrimino from the bag and handles shuffling */
static enum tetrimino_type
game_next_tetrimino(struct game *game)
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
static void
spawn_tetrimino(struct game *game, enum tetrimino_type type)
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
static void
game_clear_rows(struct game *game, int row)
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

	/* perfect line clears */
	if (row_empty(game->grid, MAX_ROW)) {
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
		game->score += score * game->level;
	}
}

/* Place the active tetrimino and check for and handle line clears */
static void
game_place_tetrimino(struct game *game)
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

static void
game_move(struct game *game, int x_offset, int y_offset)
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

static void
game_rotate(struct game *game, int rotate_by)
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

static void
game_harddrop(struct game *game)
{
	/* add two points for each cell harddropped */
	game->score += get_ghost_offset(game) * 2;
	/* swap y's with the ghost piece, dropping it as far as possible */
	game->tetrimino.y = game->ghost_y;
	game_place_tetrimino(game);
}

static void
game_hold(struct game *game)
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

/* reset game to its initial state and prepare for a new game */
static void
game_reset(struct game *game)
{
	game->running = true;
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

	/* set previous time frame to prevent instant gravity on first frame */
	clock_gettime(CLOCK_MONOTONIC, &game->time_prev);
	spawn_tetrimino(game, game_next_tetrimino(game));
}

/*** Game loop ***/

void
game_input(struct game *game)
{
	switch (getch()) {
	case KEY_LEFT:
		game_move(game, -1, 0);
		break;
	case KEY_RIGHT:
		game_move(game, 1, 0);
		break;
	case KEY_UP:
		game_move(game, 0, 1);
		break;
	case KEY_DOWN:
		game_harddrop(game);
		break;
	case 'x':
		game_rotate(game, 1);
		break;
	case 'z':
		game_rotate(game, -1);
		break;
	case 'c':
		game_hold(game);
		render_hold(game);
		break;
	case 'r':
		game_reset(game);
		break;
	case 'q':
		game->running = false;
		break;
	default:
		return;
	}

	/* TODO: this is kind of random */
	render_info(game);
	render_preview(game);
}

void
game_update(struct game *game)
{
	struct timespec time_now;
	clock_gettime(CLOCK_MONOTONIC, &time_now);

	/* delta time calculation */
	game->accumulator += diff_timespec(&time_now, &game->time_prev);
	game->time_prev = time_now;

	/* check if tetrimino can be moved down by gravity */
	if (game_tetrimino_valid(game, game->tetrimino.rotation, 0, 1)) {
		int i = game->level > 20 ? 20 : game->level - 1;
		if (game->accumulator > gravity_table[i]) {
			game->accumulator -= gravity_table[i];
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

void
game_render(const struct game *game)
{
	if (game->has_lost)
		render_gameover(game);
	else 
		/* grid changes practically every frame, so always render it */
		render_grid(game);
}

struct game *
game_create()
{
    /* ncurses initialization */
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

	/* TODO: add check if terminal supports color */
	start_color();
	/* add 2 because 0 is invalid and the enums start from -1 */
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

	struct game *game = malloc(sizeof(*game));

	/* seed random */
	srand(time(NULL));

	/* set initial state of bags */
	enum tetrimino_type initial_bag[BAGSIZE] = {I, J, L, O, S, T, Z};
	memcpy(game->bag, initial_bag, sizeof(initial_bag));
	memcpy(game->shuffle_bag, initial_bag, sizeof(initial_bag));

	game_reset(game);

	return game;
}

void
game_destroy(struct game *game)
{
	free(game);

	wclear(stdscr);
	endwin();
}