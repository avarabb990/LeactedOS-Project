# Build LeactedOS apps:   make -C sdk          (needs gcc with -m32, ld, python3)
CC      = gcc
CFLAGS  = -m32 -std=gnu11 -Os -Wall -Wextra -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -fno-builtin -fno-asynchronous-unwind-tables -fcf-protection=none -mno-sse -mno-mmx -nostdlib
APPS    = hello snake doodle minesweeper tetris game2048 pong stopwatch
OUT     = out

NAME_hello  = Hello
SIZE_hello  = 340x190
NAME_snake  = Snake
SIZE_snake  = 320x248
NAME_doodle = Doodle
SIZE_doodle = 256x184
NAME_minesweeper = Minesweeper
SIZE_minesweeper = 216x254
NAME_tetris = Tetris
SIZE_tetris = 290x336
NAME_game2048 = 2048
SIZE_game2048 = 320x380
NAME_pong = Pong
SIZE_pong = 400x280
NAME_stopwatch = Stopwatch
SIZE_stopwatch = 280x270



all: $(APPS:%=$(OUT)/%.lapp)
.SECONDARY:

$(OUT):
	mkdir -p $(OUT)

$(OUT)/leact_rt.o: leact_rt.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: apps/%.c leact.h leact_util.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.elf: $(OUT)/%.o $(OUT)/leact_rt.o app.ld
	ld -m elf_i386 --no-warn-rwx-segments -nostdlib -q -T app.ld $(OUT)/$*.o $(OUT)/leact_rt.o -o $@

$(OUT)/%.lapp: $(OUT)/%.elf ../tools/mkapp.py
	python3 ../tools/mkapp.py $< -o $@ --name "$(NAME_$*)" --size $(SIZE_$*)

clean:
	rm -rf $(OUT)
