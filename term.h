#ifndef TERM_H
#define TERM_H

/* forward declarations */
struct game;

void term_init();
void term_destroy();

void render_grid(const struct game *game);
void render_hold(const struct game *game);
void render_info(const struct game *game);
void render_preview(const struct game *game);
void render_gameover(const struct game* game);

#endif /* TERM_H */