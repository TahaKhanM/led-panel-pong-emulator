CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
.PHONY: test web clean
test: build/test_game build/test_protocol
	./build/test_game
	./build/test_protocol
build/test_game: tests/test_game.c src/game.c src/panel.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc $< -o $@
build/test_protocol: tests/test_protocol.c emulator/src/panel_emu.c src/panel.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc $< -o $@
web:
	./emulator/scripts/build_web.sh
clean:
	rm -rf build emulator/web/pong.js emulator/web/pong.wasm
