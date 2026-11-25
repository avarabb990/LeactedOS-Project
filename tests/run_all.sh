#!/bin/sh
# Build the sample apps and run every test:  sh tests/run_all.sh   (from the project folder)
set -e
cd "$(dirname "$0")/.."
make -C sdk >/dev/null
python3 tests/gen_images.py >/dev/null
# 1. real compiled apps, executed as 32-bit code at two different addresses
gcc -m32 -std=gnu11 -O1 -Wall -Wextra -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -nostdlib -static \
    -Wl,-N -Wl,--no-warn-rwx-segments -o tests/run32 tests/run32.c
for a in hello snake doodle; do ./tests/run32 sdk/out/$a.lapp tests/out/app_$a.ppm; done
# 1b. the TCP/IP stack and HTTP client against a fake network (DHCP, DNS, TCP, loss, redirects...)
gcc -m64 -O1 -g -fsanitize=address,undefined -Wall -Wextra -I. -o tests/net_test tests/net_test.c
./tests/net_test | tail -1
# 2. the whole OS (File Manager, apps, windows...) on the PC with the same app sources built natively
for a in hello snake doodle; do
    gcc -m64 -O1 -g -fsanitize=address,undefined -Dlapp_main=${a}_main -c sdk/apps/$a.c -o tests/out/$a.o
done
gcc -m64 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function -o host_test host_test.c tests/out/hello.o tests/out/snake.o tests/out/doodle.o
./host_test
