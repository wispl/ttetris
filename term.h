#ifndef TERM_H
#define TERM_H

/* forward declarations */
struct game;

enum action { 
    MOVE_LEFT,
    MOVE_RIGHT,
    ROTATE_CW,
    ROTATE_CCW,
    HOLD_PIECE,
    SOFTDROP,
    HARDDROP,
    RESTART,
    QUIT,
    OTHER
};

void term_init();
void term_destroy();
enum action term_input();

void render_grid(const struct game *game);
void render_hold(const struct game *game);
void render_info(const struct game *game);
void render_preview(const struct game *game);
void render_gameover(const struct game* game);

#endif /* TERM_H */