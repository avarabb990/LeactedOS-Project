/*
 * LAPP loader core: header checks and relocation.
 * Pure functions with no kernel dependencies, so the OS and the test
 * programs use exactly the same code.
 */
#ifndef LAPP_LOAD_H
#define LAPP_LOAD_H
#include "leact.h"

static inline uint32_t lapp_crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static inline uint32_t lapp_rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Validate a whole .lapp file.  Returns NULL if it is fine, otherwise a
 * short message for the user.  *h receives a copy of the header. */
static inline const char *lapp_check(const uint8_t *f, uint32_t len, struct lapp_header *h) {
    if (len < LAPP_HDR_SIZE) return "Not a LeactedOS app (file too small)";
    uint8_t *d = (uint8_t *)h;
    for (uint32_t i = 0; i < LAPP_HDR_SIZE; i++) d[i] = f[i];
    if (h->magic != LAPP_MAGIC) return "Not a LeactedOS app (bad signature)";
    if (h->format != LAPP_FORMAT) return "Unsupported app file version";
    if (h->api_min > LAPP_API_VERSION) return "App needs a newer LeactedOS";
    if (h->nreloc > LAPP_MAX_RELOC || h->image_size > LAPP_MAX_MEM || h->bss_size > LAPP_MAX_MEM) return "App is too big (64 KB limit)";
    if (h->image_size + h->bss_size > LAPP_MAX_MEM) return "App is too big (64 KB limit)";
    uint32_t need = LAPP_HDR_SIZE + h->image_size + h->nreloc * 4u;
    if (len < need) return "App file is truncated";
    if (h->entry_off >= h->image_size) return "App has a bad entry point";
    if (h->win_w < 64 || h->win_w > 620 || h->win_h < 48 || h->win_h > 420) return "App has a bad window size";
    if (lapp_crc32(f + LAPP_HDR_SIZE, h->image_size + h->nreloc * 4u) != h->crc32) return "App file is damaged (checksum)";
    h->name[23] = 0;
    return 0;
}

/* Copy the image into mem (LAPP_MAX_MEM bytes), clear bss and add `base`
 * (the address mem will have when the app runs) to every relocation. */
static inline const char *lapp_load(const uint8_t *f, const struct lapp_header *h, uint8_t *mem, uint32_t base) {
    const uint8_t *img = f + LAPP_HDR_SIZE, *rel = img + h->image_size;
    for (uint32_t i = 0; i < h->image_size; i++) mem[i] = img[i];
    for (uint32_t i = 0; i < h->bss_size; i++) mem[h->image_size + i] = 0;
    for (uint32_t i = 0; i < h->nreloc; i++) {
        uint32_t off = lapp_rd32(rel + i * 4u);
        if (off + 4 > h->image_size) return "App has a bad relocation";
        uint32_t v = lapp_rd32(mem + off) + base;
        mem[off] = v; mem[off + 1] = v >> 8; mem[off + 2] = v >> 16; mem[off + 3] = v >> 24;
    }
    return 0;
}
#endif
