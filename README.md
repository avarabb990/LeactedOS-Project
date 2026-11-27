# LeactedOS-Project
LeactedOS a Hobby OS made from scatch starting from a unuseable OS to a useable OS 

## Run it
```
qemu-system-i386 -kernel releases/leacteos-v9.bin -m 128 -vga std -nic user,model=rtl8139
```
Add `-drive file=disk.img,format=raw,if=ide` (up to four drives, shown as C: to F:) to give the File Manager something to browse. `-nic user,model=rtl8139` gives the OS a network card (the Realtek RTL8139 is the only one supported) for the Browser, `wget`, `ping` and `leact-get`.

### Apps from this repository (leact-get)
The [`repo/`](repo/) folder is the default online app repository. In the OS terminal: `leact-get list`, `leact-get install snake`, `run snake`.
GitHub only speaks HTTPS and LeactedOS has no TLS yet, so run the gateway on the PC that runs QEMU first:
```
python3 tools/leact-gateway.py
```
Apps available right now:

| Name | What it is |
|------|------------|
| `minesweeper` | Minesweeper 9x9, flags, chording, best time |
| `tetris` | Tetris with ghost piece, levels, high score |
| `game2048` | 2048 sliding tile puzzle (installs as "2048") |
| `pong` | Pong against the computer, first to 7 |
| `stopwatch` | Stopwatch with laps and a countdown timer |
| `snake`, `doodle`, `hello` | Snake, a drawing pad, and the SDK demo |

To add your own app: write it in `sdk/apps/`, run `make -C sdk`, then `python3 tools/mkrepo.py --dir repo sdk/out/*.lapp` and push.

See the README section "The HTTPS gateway" in [BUILDING.md](BUILDING.md). Build instructions and the app SDK guide are in [BUILDING.md](BUILDING.md).

## Versions
Every version is a git tag, so you can browse the source of any of them (`git checkout v3`). Ready-made kernels are in `releases/`.

| Tag | What it added |
|-----|---------------|
| v1 | The original kernel (`releases/leacteos-v1-original.bin`, binary only) |
| v2 | Rebuilt in C from the original binary, with bug fixes and new apps |
| v3 | Settings, Task Manager, three themes (Windows XP, Unity, KDE) |
| v4 | Calculator and the LeactedOS logo |
| v5 | File Manager (FAT12/16/32 and LeactedFS), Open With, Hex and Image viewers |
| v6 | App format (`.lapp`), loader, SDK and sample apps |
| v7 | LeactedFS v2 (64 files of 128 KB), many more terminal commands, `leact-get` from a repository disk |
| v8 | Networking (RTL8139, DHCP, DNS, TCP, HTTP), `ping`/`wget`, and the Browser |
| v9 | HTTPS through a host gateway, online `leact-get` with this repository's `repo/` folder |
| v10 | Browser: CSS and JavaScript (`css.h`, `js.h`), external `.css`/`.js` files, DOM, timers |
| v11 | Real installer: writes a boot loader and the system to a disk, which then boots on its own (no `-kernel`) |

v7, v8 and v9 were built in one go and share a single source commit (the binaries of each are in `releases/`).

The `leacteos-v*.bin` files in the top folder were uploaded by hand earlier. `leacteos-v2.bin` there is the same file as `releases/leacteos-v3.bin`, so go by the `releases/` names.

## License
MIT, see [LICENSE](LICENSE).

## Browser: CSS and JavaScript
The Browser lays pages out with a small CSS engine (`css.h`) and runs scripts with a small JavaScript interpreter (`js.h`).

**CSS:** tag, `.class`, `#id`, compound, descendant, child and comma selectors; `@media` (screen); `!important`, specificity, inline `style=""`, `<link rel=stylesheet>`. Properties: `color`, `background(-color)`, `font-weight`, `font-size` (22px and up is drawn double size), `text-align`, `display`, `visibility`, `text-transform`, `text-decoration`, `margin`/`padding` (as blank rows and indentation), `list-style`, `white-space`.

**JavaScript:** variables, functions, closures, arrow functions, template strings, spread, `for`/`while`/`switch`/`try`, arrays, objects, `Math`, `JSON.stringify`, string and array methods, `setTimeout`/`setInterval`, `localStorage` (per page), `Date`. DOM: `getElementById`, `querySelector(All)`, `innerHTML`, `textContent`, `style`, `classList`, `createElement`/`appendChild`, `onclick` and `addEventListener('click')`, `document.write`, `location.href`. `console.log` and errors print to the Terminal; `alert` shows in the status line.

**Limits:** numbers are fixed point (about 9 decimal digits, no huge values); no `class`, `async`/`await`, regular expressions, destructuring or generators (a page using them gets a script error and the rest still works); layout is text rows, so `float`, `position`, widths and images are ignored; only click events; scripts stop after 8 million steps.

## Installing LeactedOS on a disk
The Installer app (desktop icon, or `install` in the Terminal) makes a disk that boots by itself.

```
qemu-img create -f raw disk.img 64M
qemu-system-i386 -kernel leacteos-v11.bin -m 256 -nic user,model=rtl8139 -drive file=disk.img,format=raw,if=ide
```
Open **Installer**, pick the drive, click *Install Now* twice. It formats LeactedFS, writes the boot loader and a copy of the running system, copies your files, then reads everything back to check it. Close QEMU and start again **without `-kernel`**:
```
qemu-system-i386 -drive file=disk.img,format=raw,if=ide -m 256 -nic user,model=rtl8139
```
Disk layout: sector 0 boot sector + partition table, 1 LeactedFS super block, 3-7 directory, 8-15 boot loader stage 2, 16- the system image, then file data. The loader (`boot/stage1.asm`, `boot/stage2.asm`) needs a BIOS with LBA disk reads and about 20 MB of RAM; it enables A20, loads the image to 1 MB and starts it like a multiboot loader. `python3 tools/mkboot.py` rebuilds `boot_bin.h` from the sources (needs `nasm`; the generated header is checked in). `tests/boot_emu.py` boots an installed disk image on a small x86 emulator as part of `tests/run_all.sh`.
