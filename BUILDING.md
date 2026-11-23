# LeactedOS XP Edition 2.0 (v6)

## Run it
    qemu-system-i386 -kernel leacteos.bin -m 128 -vga std -drive file=disk.img,format=raw,if=ide

Up to four drives show as C: to F: (attach with more `-drive ...` lines).
Blank disk for LeactedFS: `qemu-img create -f raw disk.img 64M` (the Installer formats it).
FAT test disk: `python3 tools/mkfat.py disk.img --type 32 --src myfolder/`

## Build
    make                 build leacteos.bin (needs gcc -m32 and ld)
    make -C sdk          build the sample apps into sdk/out/*.lapp
    sh tests/run_all.sh  run every test (needs python3 and gcc)

## Apps (.lapp)
An app is a small file the OS can load and run. To try the samples:
1. Attach `apps.img` (FAT16, holds hello / snake / doodle) as a drive, open it in the
   File Manager, double-click a `.lapp` file and press **Open** on "Run as app".
2. Or copy one into LeactedFS with **Import**, then in the Terminal: `run hello`.
   Terminal commands: `run APP`, `apps` (list), `appinfo APP`.
Up to 4 apps run at once. LeactedFS holds files up to 4 KB, FAT drives have no such limit.

### Writing your own
Look at `sdk/apps/hello.c`. Every app has one entry point:

    #include "../leact_util.h"
    LAPP_MAIN {                       /* int lapp_main(api, ev) */
        switch (ev->type) {
        case EV_DRAW:  api->text(10, 10, "Hi!", api->c_text); break;
        case EV_KEY:   /* ev->key */ break;
        case EV_CLICK: /* ev->x, ev->y */ break;
        }
        return 0;
    }

Events: EV_INIT, EV_DRAW, EV_TICK (about 33 per second), EV_KEY, EV_CLICK, EV_MOVE,
EV_RELEASE, EV_CLOSE. Keep state in global variables. Drawing is only allowed during
EV_DRAW; coordinates start at the top-left of the window contents.
The API (`sdk/leact.h`): rect, frame, line, circle, pixel, text, text_scaled, button,
blit, ticks, rand, beep, set_title, close, log, file_read/write/delete/name, theme colours.

Rules for app code: no libc, no 64-bit division, no floating point, no `-fPIC`.
Add your app to `APPS` in `sdk/Makefile` (with NAME_x and SIZE_x) and run `make -C sdk`.

File format (64-byte header, then image, then relocation table): see `sdk/leact.h`
and `tools/mkapp.py`. The OS checks signature, version, sizes and a CRC-32 before running.

**Apps run with full kernel privileges.** A buggy app can freeze the OS and a malicious
one can do anything, so only run apps you trust.

## License
MIT, see LICENSE. The 8x8 font in font.h was recovered from the original LeactedOS XP kernel.
