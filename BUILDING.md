# LeactedOS XP Edition 2.0
Run:   qemu-system-i386 -kernel leacteos.bin -vga std -drive file=disk.img,format=raw,if=ide
Disk:  qemu-img create -f raw disk.img 64M   (optional; the Installer formats it as LeactedFS)
Build: make            Host UI test: gcc -m64 -O1 -DHOST host_test.c -o host_test && ./host_test
