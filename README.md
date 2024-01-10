# ttetris

Tetris in the terminal

### Features

* SRS rotation
* Holding
* Previews
* Music and Sound effects

#### Todo

- [ ] customizable keybinds?
- [ ] network?

#### Missing

* no DAS, not sure if it is possible
* gui :)

### Building

A compiler which supports at least C99 is needed
```
git clone https://github.com/wispl/ttetris.git
cd ttetris
make
```

#### Dependencies and Libraries

* ncurses
* [miniaudio](https://github.com/mackron/miniaudio) (vendored)
* [stb_vorbis](https://github.com/nothings/stb) (vendored)

### Credits and resources

* music and sound effects: [tetra_legends](https://github.com/doktorocelot/tetralegends)
* rotation & kicktable, and inspiration: [nullpomino](https://github.com/nullpomino/nullpomino)
* gravity: [tetris fandom](https://tetris.fandom.com/wiki/Tetris_Worlds) for gravity leveling
* guidelines: [tetris wiki](https://tetris.wiki/Tetris_Guideline)
* miniaudio: an excellent, simple, and featureful header library
* stb_files: providing a sane way to compile features on Windows
