#!/bin/sh
# Build the sample apps and run every test:  sh tests/run_all.sh   (from the project folder)
set -e
cd "$(dirname "$0")/.."
make -C sdk >/dev/null
python3 tests/gen_images.py >/dev/null
# 1. real compiled apps, executed as 32-bit code at two different addresses
gcc -m32 -std=gnu11 -O1 -Wall -Wextra -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdlib -static \
    -Wl,-N -Wl,--no-warn-rwx-segments -o tests/run32 tests/run32.c
for a in hello snake doodle minesweeper tetris game2048 pong stopwatch; do ./tests/run32 sdk/out/$a.lapp tests/out/app_$a.ppm | tail -1; done
# 1a. look inside the games: line clears, merges, random-play soak under ASan
for d in TETRIS 2048; do
    gcc -m64 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function -DAPP_$d -o tests/out/unit_$d tests/app_unit.c
    ./tests/out/unit_$d
done
# 1b. the TCP/IP stack and HTTP client against a fake network (DHCP, DNS, TCP, loss, redirects...)
gcc -m64 -O1 -g -fsanitize=address,undefined -Wall -Wextra -I. -o tests/net_test tests/net_test.c
./tests/net_test | tail -1
# 1c. the CSS engine and the JavaScript interpreter on their own
for t in css_test js_test; do
    gcc -m64 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-unused-function -Wno-misleading-indentation -o tests/out/$t tests/$t.c
    ./tests/out/$t | tail -1
done
# 1d. the boot loader (stage 1 + 2) is run on a small x86 emulator against the disk the Installer wrote
#     (host_test writes tests/out/installed_disk.img below; checked after it)
# 2. the whole OS (File Manager, apps, windows...) on the PC with the same app sources built natively
for a in hello snake doodle; do
    gcc -m64 -O1 -g -fsanitize=address,undefined -Dlapp_main=${a}_main -c sdk/apps/$a.c -o tests/out/$a.o
done
gcc -m64 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function -o host_test host_test.c tests/out/hello.o tests/out/snake.o tests/out/doodle.o
./host_test
# 3. boot the disk the Installer just wrote, then the install CD built from the kernel (needs python3)
python3 tests/boot_emu.py tests/out/installed_disk.img
make -s
python3 tools/mkiso.py leacteos.bin tests/out/leacteos.iso
python3 tests/boot_emu.py tests/out/leacteos.iso
