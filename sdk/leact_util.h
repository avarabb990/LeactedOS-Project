/* Small helpers for apps (all static: only what you use ends up in the app). */
#ifndef LEACT_UTIL_H
#define LEACT_UTIL_H
#include <stdarg.h>
#include <stddef.h>
#include "leact.h"

#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

#define LU_UNUSED __attribute__((unused))
LU_UNUSED static int lu_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* mini snprintf: %d %u %x %c %s %% with optional 0 and width (%02d, %5u) */
LU_UNUSED static int lu_fmt(char *out, int max, const char *f, ...) {
    va_list ap; va_start(ap, f);
    int n = 0;
#define PUT(ch) do { if (n < max - 1) out[n] = (ch); n++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++;
        int zero = 0, width = 0;
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == 's') { const char *s = va_arg(ap, const char *); while (*s) PUT(*s++); }
        else if (*f == 'c') { PUT((char)va_arg(ap, int)); }
        else if (*f == '%') { PUT('%'); }
        else if (*f == 'd' || *f == 'u' || *f == 'x') {
            uint32_t v; int neg = 0;
            if (*f == 'd') { int s = va_arg(ap, int); if (s < 0) { neg = 1; v = 0u - (uint32_t)s; } else v = s; }
            else v = va_arg(ap, uint32_t);
            uint32_t base = *f == 'x' ? 16 : 10;
            char t[12]; int k = 0;
            do { t[k++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
            int len = k + neg;
            if (!zero) for (; len < width; len++) PUT(' ');
            if (neg) PUT('-');
            if (zero) for (; len < width; len++) PUT('0');
            while (k) PUT(t[--k]);
        }
    }
    va_end(ap);
    if (max > 0) out[n < max ? n : max - 1] = 0;
    return n;
#undef PUT
}
#endif
