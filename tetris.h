#ifndef TETRIS_H
#define TETRIS_H

#include <stdbool.h>

int game_init();
void game_destroy();

bool game_running();
void game_input();
void game_update();
void game_render();
#endif /* TETRIS_H */