/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* util.c -- freestanding runtime for Shiyin.
 *
 * The player links with -nostdlib: no CRT, no msvcrt.dll, no import of any
 * helper that Windows 95 does not ship.  Everything the app (and the two
 * vendored single-header libraries) needs lives here: heap, mem/str, wide
 * string conversions, INI access and small path helpers.
 */
#include "shiyin.h"

static void mc_path(const char *ini, char *out, int cap);
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ===================================================================== */
/*  heap                                                                 */
/* ===================================================================== */

static HANDLE g_heap;

static HANDLE heap_handle(void)
{
    if (!g_heap)
        g_heap = GetProcessHeap();
    return g_heap;
}

void *malloc(size_t n)
{
    if (!n)
        n = 1;
    return HeapAlloc(heap_handle(), 0, n);
}

void free(void *p)
{
    if (p)
        HeapFree(heap_handle(), 0, p);
}

void *realloc(void *p, size_t n)
{
    if (!p)
        return malloc(n);
    if (!n) {
        free(p);
        return 0;
    }
    return HeapReAlloc(heap_handle(), 0, p, n);
}

void *calloc(size_t n, size_t size)
{
    size_t total;
    if (!n || !size)
        return malloc(1);
    total = n * size;
    if (size && total / size != n)
        return 0;
    return HeapAlloc(heap_handle(), HEAP_ZERO_MEMORY, total);
}

void *sy_alloc(u32 n)              { return malloc(n); }
void *sy_calloc(u32 n, u32 size)   { return calloc(n, size); }
void *sy_realloc(void *p, u32 n)   { return realloc(p, n); }
void  sy_free(void *p)             { free(p); }

/* ===================================================================== */
/*  mem / str                                                            */
/* ===================================================================== */

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    while (n >= 4) {
        *(u32 *)dp = *(const u32 *)sp;
        dp += 4;
        sp += 4;
        n -= 4;
    }
    while (n--)
        *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    if (dp == sp || !n)
        return d;
    if (dp < sp) {
        while (n--)
            *dp++ = *sp++;
    } else {
        dp += n;
        sp += n;
        while (n--)
            *--dp = *--sp;
    }
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return (int)*x - (int)*y;
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (!n)
        return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    while (n && *s) {
        *d++ = *s++;
        n--;
    }
    while (n--)
        *d++ = 0;
    return r;
}

char *strcat(char *d, const char *s)
{
    char *r = d;
    while (*d)
        d++;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

/* ===================================================================== */
/*  tiny libm subset (only what stb_image's JPEG/PNG paths can call)     */
/* ===================================================================== */

double ldexp(double x, int e)
{
    while (e >= 16) { x *= 65536.0; e -= 16; }
    while (e <= -16) { x *= 1.0 / 65536.0; e += 16; }
    while (e > 0) { x *= 2.0; e--; }
    while (e < 0) { x *= 0.5; e++; }
    return x;
}

double fabs(double x)
{
    union { double d; u32 u[2]; } v;
    v.d = x;
    v.u[1] &= 0x7FFFFFFFu;
    return v.d;
}

float fabsf(float x)
{
    union { float f; u32 u; } v;
    v.f = x;
    v.u &= 0x7FFFFFFFu;
    return v.f;
}

double floor(double x)
{
    if (x >= 0.0) {
        double t = (double)(long long)x;
        return t;
    } else {
        double t = (double)(long long)x;
        return (t == x) ? t : t - 1.0;
    }
}

double pow(double x, double y)
{
    /* stb_image only reaches this through its (disabled) HDR paths, but keep a
     * correct implementation so the symbol never drags in a CRT dependency. */
    double l2, r, n, f;
    if (x <= 0.0)
        return 0.0;
    __asm__ __volatile__ ("fyl2x" : "=t"(l2) : "0"(x), "u"(y) : "st(1)");
    n = floor(l2);
    f = l2 - n;
    r = 1.0 + f * (0.6931471805599453 +
        f * (0.2402265069591007 +
        f * (0.05550410866482158 +
        f * (0.009618129107628477 +
        f * 0.0013333558146428443))));
    return ldexp(r, (int)n);
}

double sqrt(double x)
{
    double r;
    __asm__ volatile ("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

/* ===================================================================== */
/*  wide strings                                                         */
/* ===================================================================== */

int w_len(const wchar_t *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

void w_copy(wchar_t *dst, const wchar_t *src, int cap)
{
    int i = 0;
    if (!dst || cap <= 0)
        return;
    if (src) {
        for (; i < cap - 1 && src[i]; i++)
            dst[i] = src[i];
    }
    dst[i] = 0;
}

void w_cat(wchar_t *dst, const wchar_t *src, int cap)
{
    int n = w_len(dst);
    w_copy(dst + n, src, cap - n);
}

void w_trim(wchar_t *s)
{
    int n = w_len(s);
    int i = 0;
    while (s[i] == L' ' || s[i] == L'\t' || s[i] == L'\r' || s[i] == L'\n')
        i++;
    if (i) {
        int k = 0;
        while (s[i])
            s[k++] = s[i++];
        s[k] = 0;
        n = k;
    }
    while (n > 0) {
        wchar_t c = s[n - 1];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0)
            s[--n] = 0;
        else
            break;
    }
}

static wchar_t w_fold(wchar_t c)
{
    if (c >= L'A' && c <= L'Z')
        return (wchar_t)(c + 32);
    if (c >= 0xFF01 && c <= 0xFF5E)   /* full-width ASCII */
        return (wchar_t)(c - 0xFF01 + 0x21);
    return c;
}

int w_ieq_ascii(const wchar_t *a, const wchar_t *b)
{
    if (!a || !b)
        return a == b;
    while (*a && *b) {
        if (w_fold(*a) != w_fold(*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int w_contains(const wchar_t *hay, const wchar_t *needle)
{
    int nl = w_len(needle);
    int i;
    if (!hay || !nl)
        return 1;
    for (i = 0; hay[i]; i++) {
        int k = 0;
        while (k < nl && hay[i + k] && w_fold(hay[i + k]) == w_fold(needle[k]))
            k++;
        if (k == nl)
            return 1;
    }
    return 0;
}

int w_from_ansi(const char *src, wchar_t *dst, int cap)
{
    int n;
    if (!dst || cap <= 0)
        return 0;
    dst[0] = 0;
    if (!src)
        return 0;
    n = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, cap);
    if (n <= 0) {
        /* Fallback: treat bytes as latin-1 so we never end up with nothing. */
        return w_from_latin1(src, (int)strlen(src), dst, cap);
    }
    dst[cap - 1] = 0;
    return n - 1;
}

int w_from_latin1(const char *src, int bytes, wchar_t *dst, int cap)
{
    int i;
    if (!dst || cap <= 0)
        return 0;
    for (i = 0; i < bytes && i < cap - 1; i++)
        dst[i] = (wchar_t)(unsigned char)src[i];
    dst[i] = 0;
    return i;
}

int w_from_utf8(const char *src, int bytes, wchar_t *dst, int cap)
{
    int i = 0, o = 0;
    if (!dst || cap <= 0)
        return 0;
    if (bytes < 0)
        bytes = (int)strlen(src);
    while (i < bytes && o < cap - 1) {
        unsigned char c = (unsigned char)src[i];
        u32 cp;
        int extra;
        if (c < 0x80) {
            dst[o++] = (wchar_t)c;
            i++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            dst[o++] = L'?';
            i++;
            continue;
        }
        if (i + extra >= bytes + 1) {
            break;
        }
        {
            int k;
            for (k = 1; k <= extra; k++) {
                unsigned char cc = (unsigned char)src[i + k];
                if ((cc & 0xC0) != 0x80) {
                    cp = 0xFFFD;
                    break;
                }
                cp = (cp << 6) | (cc & 0x3F);
            }
        }
        i += extra + 1;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            if (o < cap - 2) {
                dst[o++] = (wchar_t)(0xD800 + (cp >> 10));
                dst[o++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
            }
        } else {
            dst[o++] = (wchar_t)cp;
        }
    }
    dst[o] = 0;
    return o;
}

int w_from_utf16(const u8 *src, int bytes, int big_endian, wchar_t *dst, int cap)
{
    int i = 0, o = 0;
    if (!dst || cap <= 0)
        return 0;
    /* BOM handling */
    if (bytes >= 2) {
        if (src[0] == 0xFF && src[1] == 0xFE) {
            big_endian = 0;
            i = 2;
        } else if (src[0] == 0xFE && src[1] == 0xFF) {
            big_endian = 1;
            i = 2;
        }
    }
    while (i + 1 < bytes && o < cap - 1) {
        u16 v = big_endian ? (u16)((src[i] << 8) | src[i + 1])
                           : (u16)(src[i] | (src[i + 1] << 8));
        if (v == 0)
            break;
        dst[o++] = (wchar_t)v;
        i += 2;
    }
    dst[o] = 0;
    return o;
}

void w_from_int(int v, wchar_t *buf, int cap)
{
    wchar_t tmp[16];
    int n = 0, i = 0;
    int neg = v < 0;
    unsigned int u = neg ? (unsigned int)(-v) : (unsigned int)v;
    if (cap <= 0)
        return;
    if (!u) {
        tmp[n++] = L'0';
    }
    while (u && n < 15) {
        tmp[n++] = (wchar_t)(L'0' + (u % 10));
        u /= 10;
    }
    if (neg && i < cap - 1)
        buf[i++] = L'-';
    while (n > 0 && i < cap - 1)
        buf[i++] = tmp[--n];
    buf[i] = 0;
}

void w_ansi(const wchar_t *src, char *dst, int cap)
{
    if (!dst || cap <= 0)
        return;
    dst[0] = 0;
    if (!src)
        return;
    if (!WideCharToMultiByte(CP_ACP, 0, src, -1, dst, cap, NULL, NULL))
        dst[0] = 0;
    dst[cap - 1] = 0;
}

/* ===================================================================== */
/*  format names                                                         */
/* ===================================================================== */

const wchar_t *fmt_name(int fmt)
{
    switch (fmt) {
    case FMT_MP3:  return L"MP3";
    case FMT_WAV:  return L"WAV";
    case FMT_AIFF: return L"AIFF";
    case FMT_AU:   return L"AU";
    case FMT_FLAC: return L"FLAC";
    default:       return L"未知";
    }
}

const char *fmt_ext(int fmt)
{
    switch (fmt) {
    case FMT_MP3:  return "mp3";
    case FMT_WAV:  return "wav";
    case FMT_AIFF: return "aiff";
    case FMT_AU:   return "au";
    case FMT_FLAC: return "flac";
    default:       return "";
    }
}

/* ===================================================================== */
/*  file helpers                                                         */
/* ===================================================================== */

int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != 0xFFFFFFFFu;
}

int is_dir(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != 0xFFFFFFFFu && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int file_size_of(const char *path)
{
    HANDLE h;
    DWORD hi = 0, lo;
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    lo = GetFileSize(h, &hi);
    CloseHandle(h);
    if (lo == 0xFFFFFFFFu)
        return 0;
    return (int)lo;
}

void path_dir(const char *path, char *dir, int cap)
{
    int n = (int)strlen(path);
    int i;
    if (cap <= 0)
        return;
    dir[0] = 0;
    for (i = n - 1; i >= 0; i--) {
        if (path[i] == '\\' || path[i] == '/') {
            int k = i + 1;
            if (k >= cap)
                k = cap - 1;
            memcpy(dir, path, (size_t)k);
            dir[k] = 0;
            return;
        }
    }
    dir[0] = 0;
}

void path_base(const char *path, wchar_t *out, int cap)
{
    int n = (int)strlen(path);
    int i, start = 0;
    for (i = n - 1; i >= 0; i--) {
        if (path[i] == '\\' || path[i] == '/') {
            start = i + 1;
            break;
        }
    }
    w_from_ansi(path + start, out, cap);
}

void path_join(char *out, int cap, const char *dir, const char *name)
{
    int n;
    if (cap <= 0)
        return;
    out[0] = 0;
    strncpy(out, dir, (size_t)(cap - 1));
    out[cap - 1] = 0;
    n = (int)strlen(out);
    if (n && out[n - 1] != '\\' && out[n - 1] != '/' && n < cap - 2) {
        out[n++] = '\\';
        out[n] = 0;
    }
    strncpy(out + n, name, (size_t)(cap - n - 1));
    out[cap - 1] = 0;
}

int ext_is(const char *path, const char *ext)
{
    int n = (int)strlen(path);
    int e = (int)strlen(ext);
    int i;
    if (n <= e + 1)
        return 0;
    if (path[n - e - 1] != '.')
        return 0;
    for (i = 0; i < e; i++) {
        char a = path[n - e + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + 32);
        if (a != b)
            return 0;
    }
    return 1;
}

/* ===================================================================== */
/*  INI access                                                           */
/* ===================================================================== */

void ini_get_str(const char *ini, const char *sec, const char *key,
                 const char *def, char *out, int cap)
{
    if (cap <= 0)
        return;
    out[0] = 0;
    if (!GetPrivateProfileStringA(sec, key, def, out, (DWORD)cap, ini))
        out[0] = 0;
    out[cap - 1] = 0;
}

int ini_get_int(const char *ini, const char *sec, const char *key, int def)
{
    char buf[64];
    int v = 0, i = 0, neg = 0;
    ini_get_str(ini, sec, key, "", buf, sizeof(buf));
    if (!buf[0])
        return def;
    if (buf[0] == '-') {
        neg = 1;
        i = 1;
    }
    for (; buf[i]; i++) {
        if (buf[i] < '0' || buf[i] > '9')
            break;
        v = v * 10 + (buf[i] - '0');
    }
    return neg ? -v : v;
}

void ini_set_int(const char *ini, const char *sec, const char *key, int v)
{
    char buf[32];
    wchar_t w[16];
    w_from_int(v, w, 16);
    w_ansi(w, buf, sizeof(buf));
    WritePrivateProfileStringA(sec, key, buf, ini);
}

/* ===================================================================== */
/*  playlist                                                             */
/* ===================================================================== */

static int list_reserve(SyList *l, int want)
{
    SyTrack *p;
    int cap;
    if (want <= l->cap)
        return 1;
    cap = l->cap ? l->cap : 32;
    while (cap < want)
        cap *= 2;
    p = (SyTrack *)sy_realloc(l->items, (u32)cap * sizeof(SyTrack));
    if (!p)
        return 0;
    l->items = p;
    l->cap = cap;
    return 1;
}

int list_find(const SyList *l, const char *path)
{
    int i;
    for (i = 0; i < l->count; i++)
        if (!strcmp(l->items[i].path, path))
            return i;
    return -1;
}

int list_add(SyList *l, const char *path)
{
    SyTrack *t;
    if (!path || !path[0])
        return 0;
    if (!file_exists(path))
        return 0;
    if (list_find(l, path) >= 0)
        return 0;
    if (!list_reserve(l, l->count + 1))
        return 0;
    t = &l->items[l->count];
    memset(t, 0, sizeof(*t));
    strncpy(t->path, path, SY_PATH - 1);
    t->path[SY_PATH - 1] = 0;
    {
        HANDLE fh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fh != INVALID_HANDLE_VALUE) {
            t->file_size = (unsigned)GetFileSize(fh, NULL);
            CloseHandle(fh);
        }
    }
    if (!meta_cache_hit(path, t) && !tags_probe(t)) {
        /* Unreadable file: keep a usable row anyway. */
        path_base(path, t->title, SY_TEXT);
        t->format = FMT_UNKNOWN;
    }
    l->count++;
    return 1;
}

static int ext_supported(const char *path)
{
    return ext_is(path, "mp3") || ext_is(path, "wav") || ext_is(path, "aiff") ||
           ext_is(path, "aif") || ext_is(path, "aifc") || ext_is(path, "au") ||
           ext_is(path, "snd") || ext_is(path, "flac");
}

int list_add_tree(SyList *l, const char *path, int depth)
{
    WIN32_FIND_DATA fd;
    HANDLE h;
    char pattern[SY_PATH + 4];
    int added = 0;

    if (depth > 8)
        return 0;
    if (!is_dir(path)) {
        if (ext_supported(path))
            return list_add(l, path);
        return 0;
    }
    path_join(pattern, sizeof(pattern), path, "*");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        char full[SY_PATH];
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        path_join(full, sizeof(full), path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM))
                added += list_add_tree(l, full, depth + 1);
        } else if (ext_supported(full)) {
            added += list_add(l, full);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return added;
}

void list_clear(SyList *l)
{
    if (l->items)
        sy_free(l->items);
    l->items = 0;
    l->count = 0;
    l->cap = 0;
}

int list_load(SyList *l, const char *ini)
{
    {
        char mf[SY_PATH];
        mc_path(ini, mf, sizeof(mf));
        if (mf[0])
            meta_cache_load(mf);
    }
    char key[16];
    char buf[SY_PATH];
    wchar_t wkey[16];
    int i, loaded = 0, empty = 0;

    /* Read numbered keys directly instead of trusting "count": a missing or
     * stale count made the playlist look empty on Windows 95. */
    for (i = 0; i < 512; i++) {
        w_from_int(i, wkey, 16);
        w_ansi(wkey, key, sizeof(key));
        buf[0] = 0;
        ini_get_str(ini, "list", key, "", buf, sizeof(buf));
        if (!buf[0]) {
            if (++empty > 4)
                break;
            continue;
        }
        empty = 0;
        if (file_exists(buf) && list_add(l, buf) > 0)
            loaded++;
        else
            sy_log_str("load_skip", buf);
    }
    sy_log("load", ini_get_int(ini, "list", "count", -1), loaded);
    return loaded;
}

void list_save(const SyList *l, const char *ini)
{
    int i;
    for (i = 0; i < 512; i++) {
        char key[16];
        wchar_t wkey[8];
        w_from_int(i, wkey, 8);
        w_ansi(wkey, key, sizeof(key));
        WritePrivateProfileStringA("list", key, NULL, ini);
    }
    ini_set_int(ini, "list", "count", l->count);
    for (i = 0; i < l->count; i++) {
        char key[16];
        wchar_t wkey[8];
        w_from_int(i, wkey, 8);
        w_ansi(wkey, key, sizeof(key));
        WritePrivateProfileStringA("list", key, l->items[i].path, ini);
    }
}

/* Minimal formatter (no CRT): %s, %d, %c and literal text. */
int sprintf_lite(char *out, int cap, const char *fmt, ...)
{
    va_list ap;
    int n = 0;
    va_start(ap, fmt);
    while (*fmt && n < cap - 1) {
        if (*fmt != '%') {
            out[n++] = *fmt++;
            continue;
        }
        fmt++;
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            while (s && *s && n < cap - 1)
                out[n++] = *s++;
        } else if (*fmt == 'd') {
            int v = va_arg(ap, int);
            char tmp[16];
            int k = 0, neg = 0;
            if (v < 0) { neg = 1; v = -v; }
            do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v);
            if (neg && n < cap - 1) out[n++] = '-';
            while (k > 0 && n < cap - 1) out[n++] = tmp[--k];
        } else if (*fmt == 'c') {
            if (n < cap - 1) out[n++] = (char)va_arg(ap, int);
        } else {
            if (n < cap - 1) out[n++] = '%';
            continue;
        }
        fmt++;
    }
    va_end(ap);
    out[n] = 0;
    return n;
}

int write_file_all(const char *path, const void *data, int len)
{
    HANDLE h;
    DWORD wr = 0;
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (len > 0 && (!WriteFile(h, data, (DWORD)len, &wr, NULL) || (int)wr != len)) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    return 1;
}

void ini_set_str(const char *ini, const char *sec, const char *key, const char *v)
{
    WritePrivateProfileStringA(sec, key, v ? v : "", ini);
}

/* Case insensitive extension test, "." included in ext. */
int path_has_ext(const char *path, const char *ext)
{
    int n = (int)strlen(path), e = (int)strlen(ext), i;
    if (!path || !ext || n < e)
        return 0;
    for (i = 1; i <= e; i++) {
        char a = path[n - i], b = ext[e - i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b)
            return 0;
    }
    return 1;
}


/* ===================================================================== */
/*  metadata cache: skip tag parsing for files that did not change       */
/* ===================================================================== */

#define MC_N 2048

typedef struct {
    char    path[SY_PATH];
    int     size;
    int     dur, fmt, rate, ch, bits, bitrate;
    wchar_t title[192], artist[192], album[192];
} MCItem;

static MCItem *g_mc;
static int g_mc_n;
static int g_mc_saved = -1;

static int mc_atoi(const char *s)
{
    int v = 0;
    if (!s) return 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

/* derive "shiyin.meta" from "shiyin.ini" */
static void mc_path(const char *ini, char *out, int cap)
{
    int i, dot = -1;
    out[0] = 0;
    if (!ini || !ini[0]) return;
    strncpy(out, ini, (size_t)(cap - 1));
    out[cap - 1] = 0;
    for (i = 0; out[i]; i++) if (out[i] == '.') dot = i;
    if (dot < 0) return;
    out[dot] = 0;
    strncat(out, ".meta", (size_t)(cap - 1 - strlen(out)));
}

void meta_cache_load(const char *file)
{
    SyFile f;
    char line[SY_PATH + 640];
    int i = 0;

    if (!g_mc)
        g_mc = (MCItem *)sy_calloc(MC_N, sizeof(MCItem));
    g_mc_n = 0;
    if (!g_mc || !file || !file_exists(file))
        return;
    if (!file_map(file, &f))
        return;
    while (i < (int)f.size && g_mc_n < MC_N) {
        int n = 0, field = 0;
        MCItem *it = &g_mc[g_mc_n];
        char *parts[9], *q;
        while (i < (int)f.size && ((char *)f.base)[i] != '\r' &&
               ((char *)f.base)[i] != '\n' && n < (int)sizeof(line) - 1)
            line[n++] = ((char *)f.base)[i++];
        line[n] = 0;
        while (i < (int)f.size && (((char *)f.base)[i] == '\r' || ((char *)f.base)[i] == '\n'))
            i++;
        if (n < 8)
            continue;
        parts[0] = line;
        for (q = line; *q && field < 8; q++)
            if (*q == '\t') { *q = 0; parts[++field] = q + 1; }
        if (field < 8)
            continue;
        strncpy(it->path, parts[0], SY_PATH - 1);
        it->size = mc_atoi(parts[1]);
        it->dur = mc_atoi(parts[2]);
        it->fmt = mc_atoi(parts[3]);
        it->rate = mc_atoi(parts[4]);
        it->ch = mc_atoi(parts[5]);
        it->bits = mc_atoi(parts[6]);
        it->bitrate = mc_atoi(parts[7]);
        MultiByteToWideChar(CP_ACP, 0, parts[8], -1, it->title, 191);
        it->title[191] = 0;
        g_mc_n++;
    }
    file_unmap(&f);
    sy_log("meta_cache", g_mc_n, 0);
}

int meta_cache_hit(const char *path, SyTrack *t)
{
    int i;
    if (!g_mc || !path) return 0;
    for (i = 0; i < g_mc_n; i++) {
        if (!strcmp(g_mc[i].path, path)) {
            if (g_mc[i].size != (int)t->file_size) return 0;
            t->duration_ms = g_mc[i].dur;
            t->format = g_mc[i].fmt;
            t->sample_rate = g_mc[i].rate;
            t->channels = g_mc[i].ch;
            t->bits = g_mc[i].bits;
            t->bitrate = g_mc[i].bitrate;
            w_copy(t->title, g_mc[i].title, 192);
            return 1;
        }
    }
    return 0;
}

void meta_cache_save(const char *file, const SyList *l)
{
    HANDLE h;
    DWORD wr;
    int i;

    if (!l || !file || !l->count) return;
    h = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        SetFileAttributesA(file, FILE_ATTRIBUTE_NORMAL);
        h = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return;
    }
    for (i = 0; i < l->count; i++) {
        const SyTrack *t = &l->items[i];
        char line[SY_PATH + 640];
        char title[192];
        int n;
        title[0] = 0;
        w_ansi(t->title, title, sizeof(title));
        n = sprintf_lite(line, sizeof(line), "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\r\n",
                         t->path, (int)t->file_size, t->duration_ms, t->format,
                         t->sample_rate, t->channels, t->bits, t->bitrate, title);
        if (n > 0)
            WriteFile(h, line, (DWORD)n, &wr, NULL);
    }
    CloseHandle(h);
    g_mc_saved = l->count;
    sy_log("meta_save", l->count, 0);
}

/* write only when the number of tracks changed (add/remove), not on every
 * periodic settings save */
void meta_cache_sync(const char *ini, const SyList *l)
{
    char file[SY_PATH];
    if (!ini || !l || l->count == g_mc_saved)
        return;
    mc_path(ini, file, sizeof(file));
    if (file[0])
        meta_cache_save(file, l);
}

/* ===================================================================== */
/*  diagnostics                                                        */
/* ===================================================================== */

/* Append one line to shiyin.log next to the executable.  Kept deliberately
 * tiny: on a Windows 95 machine this log is the only way to find out which
 * step wedged, so it is always on but capped at 256 KB per run. */
static int sy_log_open(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(h, NULL);
        CloseHandle(h);
        if (size != 0xFFFFFFFFu && size > 256u * 1024u)
            DeleteFileA(path);
    }
    h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CloseHandle(h);
    return 1;
}

void sy_log(const char *tag, int a, int b)
{
    static char logpath[SY_PATH];
    HANDLE h;
    char buf[192];
    DWORD wr;
    int n = 0, i;

    if (!logpath[0]) {
        /* Prefer the exe folder, but fall back to writable places when the
         * player was started from a read-only share or a CD image. */
        char exe[SY_PATH];
        int k;
        GetModuleFileNameA(NULL, exe, SY_PATH);
        path_dir(exe, logpath, sizeof(logpath));
        k = (int)strlen(logpath);
        if (k > 0 && logpath[k - 1] == '\\')
            logpath[k - 1] = 0;
        strncat(logpath, "\\shiyin.log", sizeof(logpath) - strlen(logpath) - 1);
        if (!sy_log_open(logpath)) {
            char tmp[SY_PATH];
            GetTempPathA(SY_PATH, tmp);
            strncat(tmp, "shiyin.log", sizeof(tmp) - strlen(tmp) - 1);
            if (sy_log_open(tmp)) {
                strncpy(logpath, tmp, SY_PATH - 1);
                logpath[SY_PATH - 1] = 0;
            } else if (sy_log_open("C:\\shiyin.log")) {
                strcpy(logpath, "C:\\shiyin.log");
            } else {
                return;
            }
        }
    }
    h = CreateFileA(logpath, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    SetFilePointer(h, 0, NULL, FILE_END);
    {
        DWORD t = GetTickCount();
        char num[16];
        wchar_t w[24];
        w_from_int((int)(t % 1000000), w, 24);
        w_ansi(w, num, sizeof(num));
        buf[n++] = 't';
        buf[n++] = '=';
        for (i = 0; num[i] && n < 180; i++)
            buf[n++] = num[i];
        buf[n++] = ' ';
    }
    for (i = 0; tag[i] && n < 180; i++)
        buf[n++] = tag[i];
    {
        int k;
        for (k = 0; k < 2; k++) {
            char num[16];
            wchar_t w[24];
            w_from_int(k ? b : a, w, 24);
            w_ansi(w, num, sizeof(num));
            buf[n++] = ' ';
            for (i = 0; num[i] && n < 185; i++)
                buf[n++] = num[i];
        }
    }
    buf[n++] = 13;
    buf[n++] = 10;
    WriteFile(h, buf, (DWORD)n, &wr, NULL);
    CloseHandle(h);
}

/* Same file, but appends a string: used to record which exe is running. */
void sy_log_str(const char *tag, const char *text)
{
    static char logpath[SY_PATH];
    HANDLE h;
    char buf[512];
    DWORD wr;
    int n = 0, i;

    if (!logpath[0]) {
        char exe[SY_PATH];
        int k;
        GetModuleFileNameA(NULL, exe, SY_PATH);
        path_dir(exe, logpath, sizeof(logpath));
        k = (int)strlen(logpath);
        if (k > 0 && logpath[k - 1] == '\\')
            logpath[k - 1] = 0;
        strncat(logpath, "\\shiyin.log", sizeof(logpath) - strlen(logpath) - 1);
        if (!sy_log_open(logpath))
            return;
    }
    h = CreateFileA(logpath, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    SetFilePointer(h, 0, NULL, FILE_END);
    for (i = 0; tag[i] && n < 500; i++)
        buf[n++] = tag[i];
    buf[n++] = ' ';
    for (i = 0; text[i] && n < 500; i++)
        buf[n++] = text[i];
    buf[n++] = 13;
    buf[n++] = 10;
    WriteFile(h, buf, (DWORD)n, &wr, NULL);
    CloseHandle(h);
}

/* ===================================================================== */
/*  memory mapped files                                                  */
/* ===================================================================== */

int file_map(const char *path, SyFile *f)
{
    HANDLE h, m;
    DWORD lo, hi = 0;
    void *base;

    memset(f, 0, sizeof(*f));
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    lo = GetFileSize(h, &hi);
    if (lo == 0xFFFFFFFFu || (lo == 0 && hi == 0)) {
        CloseHandle(h);
        return 0;
    }
    m = CreateFileMappingA(h, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!m) {
        CloseHandle(h);
        return 0;
    }
    base = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!base) {
        CloseHandle(m);
        CloseHandle(h);
        return 0;
    }
    f->base = base;
    f->size = lo;
    f->file = h;
    f->map = m;
    return 1;
}

void file_unmap(SyFile *f)
{
    if (!f)
        return;
    if (f->base)
        UnmapViewOfFile(f->base);
    if (f->map)
        CloseHandle(f->map);
    if (f->file && f->file != INVALID_HANDLE_VALUE)
        CloseHandle(f->file);
    f->base = 0;
    f->map = 0;
    f->file = 0;
    f->size = 0;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *r = d;
    while (*d)
        d++;
    while (n-- && *s)
        *d++ = *s++;
    *d = 0;
    return r;
}
