#include "term.h"

#include "tetris.h"

#ifdef __linux__
#include <ncurses.h>
#elif _WIN32
#include <ncurses/ncurses.h>
#endif

/* TUI, rendering and input */

/* constants for rendering */
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

/* chtype for rendering block of given tetrimino type, handles indicing for
 * color pairs, prefer over manually getting chtype */
static inline chtype
tetrimino_block(enum tetrimino_type type)
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
		int c = tetrimino_block(type);

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
		int c = ghost ? '/' : tetrimino_block(game->tetrimino.type);

		/* do not render rows above 0 */
		if (y < 0)
			continue;

		mvwaddch(windows[GRID], BORDER_WIDTH + y, BORDER_WIDTH + x, c);
		waddch(windows[GRID], c);
	}
}

void
render_grid(const struct game *game)
{
	for (int y = 0; y < MAX_ROW; ++y) {
		wmove(windows[GRID], 1 + y, 1);
		for (int x = 0; x < MAX_COL; ++x) {
			chtype c = tetrimino_block(game->grid[y][x]);
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

void
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

void
render_hold(const struct game *game)
{
	werase(windows[HOLD]);
	render_tetrimino(windows[HOLD], game->hold, 0);
	box(windows[HOLD], 0, 0);
	wrefresh(windows[HOLD]);
}

void
render_info(const struct game *game)
{
	werase(windows[INFO]);
	mvwprintw(windows[INFO], 1, 1, "Lines: %d", game->lines_cleared);
	mvwprintw(windows[INFO], 2, 1, "Level: %d", game->level);
	mvwprintw(windows[INFO], 3, 1, "Score: %d", game->score);
	mvwprintw(windows[INFO], 4, 1, "Combo: %d", game->combo);
	wrefresh(windows[INFO]);
}

void
render_gameover(const struct game* game)
{
    werase(windows[GRID]);
    mvwprintw(windows[GRID], 5, 5, "You lost!\n Press R to restart");
    box(windows[GRID], 0, 0);
    wrefresh(windows[GRID]);
}

enum action term_input()
{
	switch (getch()) {
	case KEY_LEFT:   return MOVE_LEFT;
	case KEY_RIGHT:  return MOVE_RIGHT;
	case KEY_UP:     return SOFTDROP;
	case KEY_DOWN:   return HARDDROP;
	case 'x': 		 return ROTATE_CW;
	case 'z': 		 return ROTATE_CCW;
	case 'c': 		 return HOLD_PIECE;
	case 'r': 		 return RESTART;
	case 'q': 		 return QUIT;
	default: 		 return OTHER;
	}
}

void
term_init()
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
}

void
term_destroy()
{
	wclear(stdscr);
	endwin();
}
