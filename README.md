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

v7, v8 and v9 were built in one go and share a single source commit (the binaries of each are in `releases/`).

The `leacteos-v*.bin` files in the top folder were uploaded by hand earlier. `leacteos-v2.bin` there is the same file as `releases/leacteos-v3.bin`, so go by the `releases/` names.

## License
MIT, see [LICENSE](LICENSE).
