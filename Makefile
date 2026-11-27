CC=gcc
CFLAGS=-m32 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin -mno-sse -mno-mmx -nostdlib
all: leacteos.bin
kernel.o: kernel.c font.h sintab.h logo.h net.h css.h js.h boot_bin.h sdk/leact.h sdk/lapp_load.h
	$(CC) $(CFLAGS) -c kernel.c -o $@
boot.o: boot.S
	$(CC) -m32 -Wa,--noexecstack -c boot.S -o $@
leacteos.bin: boot.o kernel.o linker.ld
	ld -m elf_i386 -nostdlib -T linker.ld boot.o kernel.o -o $@
host: kernel.c font.h sintab.h net.h sdk/leact.h sdk/lapp_load.h host_test.c
	gcc -m64 -O1 -DHOST -Wall -o host_test host_test.c
clean:
	rm -f *.o leacteos.bin host_test
