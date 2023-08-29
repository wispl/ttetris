#include "tetris.h"

#include <ncurses.h>

#include <stdbool.h>
#include <time.h>

#define BORDER_WIDTH 1
#define CELL_WIDTH 2

/* Rendering logic is here, for game logic see tetris.c */
void enable_color();
chtype get_tetrimino_color(enum tetrimino_type type);
void render_tetrimino(WINDOW *w,
		      enum tetrimino_type type,
		      int rotation,
		      int y,
		      int x,
		      chtype c);
void render_current_piece(WINDOW *w, game *game);
void render_ghost_piece(WINDOW *w, game *game);
void render_grid(WINDOW *w, game *game);
void render_preview(WINDOW *w, game *game);
void render_hold(WINDOW *w, game *game);
WINDOW *window_create(int lines, int cols, int begin_y, int begin_x);

int main(void)
{
	/* start ncurses */
	initscr();
	cbreak();
	noecho();
	curs_set(0);

	/* do not wait for input */
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);

	enable_color();

	bool running = true;
	game *game = game_create();
	WINDOW *grid = window_create(MAX_ROW + 2 * BORDER_WIDTH,
				     (MAX_COL + BORDER_WIDTH) * CELL_WIDTH,
				     (LINES - MAX_ROW) / 2,
				     (COLS / 2) - MAX_COL);

	WINDOW *preview = window_create(3 * MAX_PREVIEW + 2 * BORDER_WIDTH,
					4 * CELL_WIDTH + 2 * BORDER_WIDTH,
					(LINES - MAX_ROW) / 2,
					(COLS / 2) + MAX_COL + 5);

	WINDOW *hold = window_create(3 + 2 * BORDER_WIDTH,
				     4 * CELL_WIDTH + 2 * BORDER_WIDTH,
				     (LINES - MAX_ROW) / 2,
				     (COLS / 2) - MAX_COL - 4 * CELL_WIDTH - 5);

	struct timespec time_prev, time_now;
	clock_gettime(CLOCK_REALTIME, &time_prev);

	while (running) {
		/* get frames since last calculation */
		clock_gettime(CLOCK_REALTIME, &time_now);
		int frames = 1 / (time_now.tv_sec - time_prev.tv_sec) * 100;
		time_prev = time_now;
		game_tick(game, frames);

		render_grid(grid, game);
		render_preview(preview, game);

		switch (getch()) {
		case 'q':
			running = false;
			break;
		case KEY_LEFT:
			game_move_tetrimino(game, -1, 0);
			break;
		case KEY_RIGHT:
			game_move_tetrimino(game, 1, 0);
			break;
		case KEY_DOWN:
			game_harddrop_tetrimino(game);
			break;
		case KEY_UP:
			game_move_tetrimino(game, 0, 1);
			break;
		case 'x':
			game_rotate_tetrimino(game, 1);
			break;
		case 'z':
			game_rotate_tetrimino(game, -1);
			break;
		case 'c':
			game_hold_tetrimino(game);
			render_hold(hold, game);
			break;
		}
	}

	wclear(stdscr);
	endwin();
	game_destroy(game);
	return 0;
}

/**
 * @brief Enable colors
 */
void enable_color()
{
	/* TODO: add check if terminal supports color */
	start_color();

	/* since the piece enum starts at 0, we add 1 since init_pair() only
	 * supports 1..COLOR_PAIRS-1 */
	init_pair(I + 1, COLOR_CYAN, COLOR_BLACK);
	init_pair(J + 1, COLOR_BLUE, COLOR_BLACK);
	init_pair(L + 1, COLOR_WHITE, COLOR_BLACK);
	init_pair(O + 1, COLOR_YELLOW, COLOR_BLACK);
	init_pair(S + 1, COLOR_GREEN, COLOR_BLACK);
	init_pair(T + 1, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(Z + 1, COLOR_RED, COLOR_BLACK);
}

/**
 * @brief Gets the tetrimino color
 *
 * @return chtype suitable for rendering the block
 */
inline chtype get_tetrimino_color(enum tetrimino_type type)
{
	/* add one to color pairs being 1 more than type */
	return ' ' | A_REVERSE | COLOR_PAIR(type + 1);
}

void render_tetrimino(WINDOW *w,
		      enum tetrimino_type type,
		      int rotation,
		      int y,
		      int x,
		      chtype c)
{
	for (int n = 0; n < 4; n++) {
		const int *offset = ROTATIONS[type][rotation][n];
		mvwaddch(w,
			 BORDER_WIDTH + y + offset[1],
			 BORDER_WIDTH + (x + offset[0]) * CELL_WIDTH,
			 c);
		waddch(w, c);
	}
}

void render_current_piece(WINDOW *w, game *game)
{
	render_tetrimino(w,
			 game->tetrimino.type,
			 game->tetrimino.rotation,
			 game->tetrimino.y,
			 game->tetrimino.x,
			 get_tetrimino_color(game->tetrimino.type));
}

void render_ghost_piece(WINDOW *w, game *game)
{
	render_tetrimino(w,
			 game->tetrimino.type,
			 game->tetrimino.rotation,
			 game_ghost_y(game),
			 game->tetrimino.x,
			 '/');
}

void render_grid(WINDOW *w, game *game)
{
	for (int y = 0; y < MAX_ROW; ++y) {
		wmove(w, 1 + y, 1);
		for (int x = 0; x < MAX_COL; ++x) {
			enum tetrimino_type t = game->grid[y][x];
			if (t == EMPTY) {
				waddch(w, ' ');
				waddch(w, ' ');
			} else {
				waddch(w, get_tetrimino_color(t));
				waddch(w, get_tetrimino_color(t));
			}
		}
	}

	render_ghost_piece(w, game);
	render_current_piece(w, game);
	box(w, 0, 0);
	wrefresh(w);
}

void render_preview(WINDOW *w, game *game)
{
	werase(w);
	for (int p = 0; p < MAX_PREVIEW; ++p) {
		enum tetrimino_type t = game_get_preview(game, p);
		render_tetrimino(w, t, 0, p * 3, 0, get_tetrimino_color(t));
	}
	box(w, 0, 0);
	wrefresh(w);
}

void render_hold(WINDOW *w, game *game)
{
	werase(w);
	render_tetrimino(
		w, game->hold, 0, 0, 0, get_tetrimino_color(game->hold));
	box(w, 0, 0);
	wrefresh(w);
}

WINDOW
*window_create(int lines, int cols, int begin_y, int begin_x)
{
	WINDOW *window = newwin(lines, cols, begin_y, begin_x);
	box(window, 0, 0);
	return window;
}