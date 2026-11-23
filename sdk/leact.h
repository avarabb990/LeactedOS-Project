/*
 * LeactedOS app SDK  -  shared definitions (app side AND kernel side)
 *
 * An app is a "LAPP" file:  [64-byte header][image][relocation table]
 * The kernel loads the image into a free app slot, applies the relocations,
 * then calls  lapp_main(api, event)  once for every event (see below).
 * Apps keep their state in ordinary global variables.
 *
 * Apps run in the kernel's address space with full privileges.  Only run
 * apps you trust: a buggy app can freeze the whole OS.
 */
#ifndef LEACT_H
#define LEACT_H
#include <stdint.h>

#define LAPP_MAGIC        0x5050414Cu     /* "LAPP" (little endian) */
#define LAPP_FORMAT       1
#define LAPP_API_VERSION  1
#define LAPP_HDR_SIZE     64
#define LAPP_MAX_MEM      65536u          /* image + bss must fit in 64 KB */
#define LAPP_MAX_RELOC    4096u

struct lapp_header {
    uint32_t magic;          /* LAPP_MAGIC                                    */
    uint16_t format;         /* LAPP_FORMAT                                   */
    uint16_t flags;          /* reserved, 0                                   */
    uint32_t api_min;        /* lowest kernel API version the app needs       */
    uint32_t entry_off;      /* offset of lapp_main inside the image          */
    uint32_t image_size;     /* bytes of code+data stored in the file         */
    uint32_t bss_size;       /* extra zeroed bytes after the image            */
    uint32_t nreloc;         /* number of 32-bit relocations                  */
    uint32_t crc32;          /* CRC-32 of image + relocation table            */
    uint16_t win_w, win_h;   /* size of the window's client area              */
    uint32_t reserved;
    char     name[24];       /* window title                                  */
} __attribute__((packed));
_Static_assert(sizeof(struct lapp_header) == LAPP_HDR_SIZE, "LAPP header must be 64 bytes");

/* ---- events -------------------------------------------------------- */
enum {
    EV_INIT = 1,    /* once, after loading; return non-zero to abort      */
    EV_DRAW,        /* draw the window contents (coordinates start 0,0)   */
    EV_TICK,        /* once per frame (~33 per second), even if hidden    */
    EV_KEY,         /* ev->key                                            */
    EV_CLICK,       /* left button pressed at ev->x, ev->y                */
    EV_MOVE,        /* mouse moved with the button held                   */
    EV_RELEASE,     /* left button released                               */
    EV_CLOSE        /* window is being closed; last call                  */
};

struct leact_event {
    int type;
    int x, y;       /* mouse position relative to the window contents     */
    int key;        /* key code (ASCII, or one of LK_*)                   */
    int buttons;    /* bit 0 = left button                                */
    int w, h;       /* current size of the window contents                */
};

/* key codes: printable keys are plain ASCII; '\n' Enter, '\b' Backspace, '\t' Tab */
#define LK_ESC   27
#define LK_UP    0x100
#define LK_DOWN  0x101
#define LK_LEFT  0x102
#define LK_RIGHT 0x103
#define LK_DEL   0x104
#define LK_PGUP  0x105
#define LK_PGDN  0x106
#define LK_HOME  0x107
#define LK_END   0x108

/* ---- kernel services ----------------------------------------------- */
/* Colours are 0xRRGGBB. Drawing is only allowed while handling EV_DRAW;
 * coordinates are relative to the window contents and clipped to it.   */
struct leact_api {
    uint32_t size;                    /* sizeof(struct leact_api)          */
    uint32_t version;                 /* LAPP_API_VERSION                  */

    void (*rect)(int x, int y, int w, int h, uint32_t c);        /* filled */
    void (*frame)(int x, int y, int w, int h, uint32_t c);       /* outline */
    void (*line)(int x0, int y0, int x1, int y1, uint32_t c);
    void (*circle)(int cx, int cy, int r, uint32_t c);           /* filled */
    void (*pixel)(int x, int y, uint32_t c);
    int  (*text)(int x, int y, const char *s, uint32_t c);       /* 8x8 font, returns x after the text */
    int  (*text_scaled)(int x, int y, const char *s, uint32_t c, int scale);
    void (*button)(int x, int y, int w, int h, const char *label, int pressed);
    void (*blit)(int x, int y, int w, int h, const uint32_t *argb);  /* alpha in the top byte */

    uint32_t (*ticks)(void);          /* 100 per second since boot         */
    uint32_t (*rand)(void);
    void (*beep)(uint32_t hz, uint32_t ms);                      /* blocks for ms */
    void (*set_title)(const char *s);
    void (*close)(void);              /* close this app's window           */
    void (*log)(const char *s);       /* print to the Terminal             */

    /* LeactedFS files (names are case-insensitive, max 23 characters) */
    int  (*file_read)(const char *name, void *buf, uint32_t max);  /* bytes read, or -1 */
    int  (*file_write)(const char *name, const void *buf, uint32_t len);  /* 0 ok, -1 error; max 4096 bytes */
    int  (*file_delete)(const char *name);                          /* 0 ok, -1 */
    int  (*file_name)(int index, char *out, uint32_t max);          /* size of file #index, or -1 */

    /* current theme colours (refreshed before every event) */
    uint32_t c_face, c_text, c_light, c_dark;
};

/* Every app defines exactly one entry point with this macro. */
#define LAPP_MAIN \
    __attribute__((section(".text.entry"), used)) \
    int lapp_main(const struct leact_api *api, const struct leact_event *ev)

#endif
