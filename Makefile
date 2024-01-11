.POSIX:
CC = cc
CFLAGS = -Wextra -Wall -Wpedantic -Wdouble-promotion
LDLIBS = -lpthread -lm -ldl -lncurses
OBJECTS = tetris.o miniaudio.o
CHECK_FILES = main.c tetris.c tetris.h

ifeq ($(OS),Windows_NT)
	LDLIBS = -lncurses -lPathcch
	LDFLAGS = -DNCURSES_STATIC -static
endif

all: tetris
tetris: main.c $(OBJECTS)
	$(CC) $(LDFLAGS) main.c $(OBJECTS) $(LDLIBS) -o tetris
tetris.o: tetris.c tetris.h
	$(CC) -c $(CFLAGS) tetris.c
miniaudio.o: extern/miniaudio.c extern/miniaudio.h
	$(CC) -c $(CFLAGS) extern/miniaudio.c
clean:
	rm -f tetris $(OBJECTS)
check: $(CHECK_FILES)
	clang-tidy $(CHECK_FILES) -- $(CFLAGS)
