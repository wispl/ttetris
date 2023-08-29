SOURCES := main.c tetris.c tetris.h
tetris:
	cc $(SOURCES) $(CFLAGS) -lncurses -o $@ 

clean:
	rm tetris

check: $(SOURCES)
	clang-tidy $(SOURCES) -checks=-*,clang-analyzer-*,-clang-analyzer-cplusplus*
