# ttetris

Tetris in the terminal

### Features

* Tetris, in the terminal
* SRS rotation
* Holding
* Previews

#### Todo

- [ ] frame code seems incorrect, investigate later
- [ ] better line clearing code
- [ ] leveling and scoring
- [ ] allow the player to lose
- [ ] restart
- [ ] customizable keybinds?
- [ ] network?

#### Missing

* no DAS, not sure if it is possible
* multiple keypresses at once
* gui

### Building

A compiler which supports at least C99 is needed
```
git clone https://github.com/wispl/ttetris.git
cd ttetris
make
```

#### Dependencies

ncurses

### Credits

* [nullpomino](https://github.com/nullpomino/nullpomino) for the rotation and kicktable data
* [tetris fandom](https://tetris.fandom.com/wiki/Tetris_Worlds) for gravity leveling
* [tetris wiki](https://tetris.wiki/Tetris_Guideline) for the guidelines
