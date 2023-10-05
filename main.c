#include "tetris.h"

#ifdef __linux__
#include <ncurses.h>
#elif _WIN32
#include <ncurses/ncurses.h>
#endif

#include <stdbool.h>
#include <time.h>

#define BORDER_WIDTH  1
#define CELL_WIDTH    2

#define GRID_X        (COLS / 2) - (MAX_ROW / 2)
#define GRID_Y        (LINES - MAX_ROW) / 2
#define GRID_HEIGHT   MAX_ROW + 2 * BORDER_WIDTH
#define GRID_WIDTH    MAX_COL * CELL_WIDTH + BORDER_WIDTH * 2

/* space needed to place tetrmino within a box */
#define BOX_WIDTH     4 * CELL_WIDTH + 2 * BORDER_WIDTH
#define BOX_HEIGHT    3 + 2 * BORDER_WIDTH


/* Rendering logic is here, for game logic see tetris.c */
void enable_colors();
chtype tetrimino_block(enum tetrimino_type type);
void render_tetrimino(WINDOW *w, enum tetrimino_type type, int y_offset);
void render_active_tetrimino(WINDOW *w, game *game, bool ghost);
void render_grid(WINDOW *w, game *game);
void render_preview(WINDOW *w, game *game);
void render_hold(WINDOW *w, game *game);
void render_info(WINDOW *w, game *game);

int main(void)
{
	/* start ncurses */
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);

	/* do not wait for input */
	nodelay(stdscr, TRUE);

	enable_colors();

	bool running = true;
	game *game = game_create();

	WINDOW *grid    = newwin(GRID_HEIGHT, GRID_WIDTH, GRID_Y, GRID_X);
	WINDOW *preview = newwin(MAX_PREVIEW * BOX_HEIGHT, BOX_WIDTH,
							 GRID_Y, GRID_X + GRID_WIDTH + 3);
	WINDOW *hold    = newwin(BOX_HEIGHT, BOX_WIDTH, 
							 GRID_Y, GRID_X - BOX_WIDTH - 5);
	WINDOW *info    = newwin(BOX_HEIGHT, BOX_WIDTH, 
							 LINES / 2, GRID_X - BOX_WIDTH - 5);

	struct timespec time_prev, time_now;
	clock_gettime(CLOCK_REALTIME, &time_prev);

	while (running) {
		clock_gettime(CLOCK_REALTIME, &time_now);
		game_update(game, time_now.tv_sec - time_prev.tv_sec);
		time_prev = time_now;

		if (game->has_lost) {
			werase(grid);
			mvwprintw(grid, 5, 5, "You lost!\n Press R to restart");
			box(grid, 0, 0);
			wrefresh(grid);
			if (getch() == 'r')
				game = game_create();
		}

		if (!game->has_lost) {
			render_grid(grid, game);
			render_preview(preview, game);
			render_info(info, game);

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
	}

	wclear(stdscr);
	endwin();
	game_destroy(game);
	return 0;
}

void enable_colors()
{
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
}

/* chtype for rendering block of given tetrimino type, handles indicing for
 * color pairs, prefer over manually getting chtype */
inline chtype tetrimino_block(enum tetrimino_type type)
{
	return ' ' | A_REVERSE | COLOR_PAIR(type + 2);
}

/* renders a tetrimino of type with offset y, this renders *any* tetrimino */
void render_tetrimino(WINDOW *w, enum tetrimino_type type, int y_offset)
{
	for (int n = 0; n < 4; ++n) {
		const int *offset = ROTATIONS[type][0][n];
		int x = BORDER_WIDTH + (offset[0] * 2);
		int y = BORDER_WIDTH + offset[1] + y_offset;
		int c = tetrimino_block(type);

		mvwaddch(w, y, x, c);
		waddch(w, c);
	}
}

/* renders the active tetrimino as either a ghost piece or regular piece. This
 * duplication is due to cells not being rendered above row 20 */
void render_active_tetrimino(WINDOW *w, game *game, bool ghost)
{
	for (int n = 0; n < 4; ++n) {
		const int *offset = ROTATIONS[game->tetrimino.type][game->tetrimino.rotation][n];

		int x = (game->tetrimino.x + offset[0]) * 2;
		int y = ghost ? game_ghost_y(game) + offset[1] : game->tetrimino.y + offset[1];
		int c = ghost ? '/' : tetrimino_block(game->tetrimino.type);

		/* do not render rows above 0 */
		if (y < 0)
			continue;

		mvwaddch(w, BORDER_WIDTH + y, BORDER_WIDTH + x, c);
		waddch(w, c);
	}
}

void render_grid(WINDOW *w, game *game)
{
	for (int y = 0; y < MAX_ROW; ++y) {
		wmove(w, 1 + y, 1);
		for (int x = 0; x < MAX_COL; ++x) {
			chtype c = tetrimino_block(game->grid[y][x]);
			waddch(w, c);
			waddch(w, c);
		}
	}

	/* Order is important, current piece should shadow the ghost piece */
	/* Ghost piece*/
	render_active_tetrimino(w, game, true);
	/* Current piece */
	render_active_tetrimino(w, game, false);

	box(w, 0, 0);
	wrefresh(w);
}

void render_preview(WINDOW *w, game *game)
{
	werase(w);
	for (int p = 0; p < MAX_PREVIEW; ++p)
		render_tetrimino(w, game_get_preview(game, p), p * 3);
	box(w, 0, 0);
	wrefresh(w);
}

void render_hold(WINDOW *w, game *game)
{
	werase(w);
	render_tetrimino(w, game->hold, 0);
	box(w, 0, 0);
	wrefresh(w);
}

void render_info(WINDOW *w, game *game) {
	werase(w);
	mvwprintw(w, 1, 1, "Lines: %d", game->lines_cleared);
	mvwprintw(w, 2, 1, "Level: %d", game->level);
	wrefresh(w);
}