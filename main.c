#include "tetris.h"

#ifdef __linux__
#include <ncurses.h>
#elif _WIN32
#include <ncurses/ncurses.h>
#endif

#include <stdbool.h>
#include <time.h>

#define MINIAUDIO_IMPLEMENTATION
#include "extern/miniaudio.h"

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
#define INFO_H        5

/* Rendering and music is here, for game logic see tetris.c */
void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame);
void enable_colors();
chtype tetrimino_block(enum tetrimino_type type);
void render_tetrimino(WINDOW *w, enum tetrimino_type type, int y_offset);
void render_active_tetrimino(game *game, bool ghost);
void render_grid(game *game);
void render_preview(game *game);
void render_hold(game *game);
void render_info(game *game);

enum window_type { GRID, PREVIEW, HOLD, INFO, NWINDOWS };
WINDOW* windows[NWINDOWS];

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

    windows[GRID] = newwin(GRID_H, GRID_W, GRID_Y, GRID_X);
    windows[HOLD] = newwin(BOX_H, BOX_W, GRID_Y, GRID_X - BOX_W - 5);
    windows[INFO] = newwin(INFO_H, INFO_W, LINES / 2, GRID_X - INFO_W);

    windows[PREVIEW] = newwin(N_PREVIEW * BOX_H, BOX_W, GRID_Y, GRID_X + GRID_W);

	/* audio */
	ma_result result;
	ma_decoder decoder;
	ma_device_config device_config;
	ma_device device;

	result = ma_decoder_init_file("Tetris.mp3", NULL, &decoder);
	if (result != MA_SUCCESS)
		return -1;

    /* set looping */
    ma_data_source_set_looping(&decoder, MA_TRUE);

    device_config = ma_device_config_init(ma_device_type_playback);
    device_config.noPreSilencedOutputBuffer = true;
    device_config.playback.format   = decoder.outputFormat;
    device_config.playback.channels = decoder.outputChannels;
    device_config.sampleRate        = decoder.outputSampleRate;
    device_config.dataCallback      = data_callback;
    device_config.pUserData         = &decoder;
    device_config.noClip            = true;

    if (ma_device_init(NULL, &device_config, &device) != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        return -1;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
        return -1;
    }

    struct timespec time_prev, time_now;
    clock_gettime(CLOCK_REALTIME, &time_prev);
    while (running) {
		clock_gettime(CLOCK_REALTIME, &time_now);
		game_update(game, time_now.tv_sec - time_prev.tv_sec);
		time_prev = time_now;

		if (game->has_lost) {
			werase(windows[GRID]);
			mvwprintw(windows[GRID], 5, 5, "You lost!\n Press R to restart");
			box(windows[GRID], 0, 0);
			wrefresh(windows[GRID]);
			if (getch() == 'r')
				game_reset(game);
		}

		if (!game->has_lost) {
			render_grid(game);
			render_preview(game);
			render_info(game);

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
				render_hold(game);
				break;
			}
		}
	}

	ma_device_uninit(&device);
	ma_decoder_uninit(&decoder);
	wclear(stdscr);
	endwin();
	game_destroy(game);
	return 0;
}

void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame)
{
	ma_decoder* decoder = device->pUserData;
	if (decoder == NULL)
		return;
	
	ma_data_source_read_pcm_frames(decoder, output, frame, NULL);
	(void) input;
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
void render_active_tetrimino(game *game, bool ghost)
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

void render_grid(game *game)
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

void render_preview(game *game)
{
	werase(windows[PREVIEW]);
	for (int p = 0; p < N_PREVIEW; ++p)
		render_tetrimino(windows[PREVIEW], game_get_preview(game, p), p * 3);

	box(windows[PREVIEW], 0, 0);
	wrefresh(windows[PREVIEW]);
}

void render_hold(game *game)
{
	werase(windows[HOLD]);
	render_tetrimino(windows[HOLD], game->hold, 0);
	box(windows[HOLD], 0, 0);
	wrefresh(windows[HOLD]);
}

void render_info(game *game) {
	werase(windows[INFO]);
	mvwprintw(windows[INFO], 1, 1, "Lines: %d", game->lines_cleared);
	mvwprintw(windows[INFO], 2, 1, "Level: %d", game->level);
	mvwprintw(windows[INFO], 3, 1, "Score: %d", game->score);
	mvwprintw(windows[INFO], 4, 1, "Combo: %d", game->combo);
	wrefresh(windows[INFO]);
}