#include "tetris.h"

int
main(void)
{
	game_init();
	game_mainloop();
	game_destroy();
	return 0;
}
