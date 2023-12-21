#include "tetris.h"

int
main(void)
{
    game_init();
    while (game_running()) {
		game_input();
		game_update();
		game_render();
	}
	game_destroy();
	return 0;
}
