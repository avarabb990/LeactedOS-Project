# LeactedOS-Project
LeactedOS a Hobby OS made from scatch starting from a unuseable OS to a useable OS 

## Run it
```
qemu-system-i386 -kernel releases/leacteos-v6.bin -m 128 -vga std
```
Add `-drive file=disk.img,format=raw,if=ide` (up to four drives, shown as C: to F:) to give the File Manager something to browse. `leacteos-v6-apps.img` is a small FAT16 disk with sample apps. Build instructions and the app SDK guide are in [BUILDING.md](BUILDING.md).

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

The `leacteos-v*.bin` files in the top folder were uploaded by hand earlier. `leacteos-v2.bin` there is the same file as `releases/leacteos-v3.bin`, so go by the `releases/` names.

## License
MIT, see [LICENSE](LICENSE).
