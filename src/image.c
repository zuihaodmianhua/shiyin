/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* image.c -- cover art decoding (JPEG/PNG/BMP) and square cover-crop scaling. */
#include "shiyin.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#define STBI_NO_TGA
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_FAILURE_STRINGS
#define STBI_THREAD_LOCAL
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(sz) sy_alloc((u32)(sz))
#define STBI_REALLOC_SIZED(p, oldsz, newsz) sy_realloc((p), (u32)(newsz))
#define STBI_FREE(p) sy_free(p)
#include "../third_party/stb_image.h"

static HBITMAP dib_from_rgba(const u8 *rgba, int w, int h, int stride)
{
    BITMAPINFO bi;
    void *bits = 0;
    HDC dc = GetDC(NULL);
    HBITMAP bmp;
    int y;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;          /* bottom-up */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!bmp || !bits)
        return 0;

    for (y = 0; y < h; y++) {
        const u8 *src = rgba + (size_t)(h - 1 - y) * stride;
        u8 *dst = (u8 *)bits + (size_t)y * w * 4;
        int x;
        for (x = 0; x < w; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 0;
        }
    }
    return bmp;
}

/* Center-crop to a square then bilinear-resample to box x box. */
static HBITMAP square_thumb(const u8 *rgba, int sw, int sh, int box)
{
    int side = sw < sh ? sw : sh;
    int ox = (sw - side) / 2;
    int oy = (sh - side) / 2;
    u8 *out;
    HBITMAP bmp;
    long long step = ((long long)side << 16) / (box > 0 ? box : 1);
    int y;

    out = (u8 *)sy_alloc((u32)box * box * 4);
    if (!out)
        return 0;
    for (y = 0; y < box; y++) {
        long long fy = (y * step) >> 16;
        int y0 = (int)fy;
        int y1 = y0 + 1;
        int wy = (int)(((y * step) >> 8) & 0xFF);
        int x;
        if (y1 >= side) y1 = side - 1;
        if (y0 >= side) y0 = side - 1;
        for (x = 0; x < box; x++) {
            long long fx = (x * step) >> 16;
            int x0 = (int)fx;
            int x1 = x0 + 1;
            int wx = (int)(((x * step) >> 8) & 0xFF);
            const u8 *p00, *p10, *p01, *p11;
            int c;
            if (x1 >= side) x1 = side - 1;
            if (x0 >= side) x0 = side - 1;
            p00 = rgba + ((size_t)(oy + y0) * sw + (ox + x0)) * 4;
            p10 = rgba + ((size_t)(oy + y0) * sw + (ox + x1)) * 4;
            p01 = rgba + ((size_t)(oy + y1) * sw + (ox + x0)) * 4;
            p11 = rgba + ((size_t)(oy + y1) * sw + (ox + x1)) * 4;
            for (c = 0; c < 3; c++) {
                int a = p00[c] + (((p10[c] - p00[c]) * wx) >> 8);
                int b = p01[c] + (((p11[c] - p01[c]) * wx) >> 8);
                out[(y * box + x) * 4 + c] = (u8)(a + (((b - a) * wy) >> 8));
            }
            out[(y * box + x) * 4 + 3] = 255;
        }
    }
    bmp = dib_from_rgba(out, box, box, box * 4);
    sy_free(out);
    return bmp;
}

int art_from_memory(const u8 *data, int len, int box, SyArt *out)
{
    int w = 0, h = 0, comp = 0;
    u8 *rgba;
    HBITMAP bmp;

    memset(out, 0, sizeof(*out));
    if (!data || len <= 0 || box <= 0)
        return 0;
    {
        /* Refuse oversized artwork before decoding: a 4000x3000 JPEG takes
         * seconds on a Pentium and can exhaust a Windows 95 machine's RAM,
         * which then looks like the player has frozen. */
        int iw = 0, ih = 0, ic = 0;
        int win9x = ((int)GetVersion() & 0xFF) < 5;
        long long cap = win9x ? 1500000LL : 24000000LL;
        if (stbi_info_from_memory(data, len, &iw, &ih, &ic) &&
            (long long)iw * ih > cap)
            return 0;
    }
    rgba = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
    if (!rgba || w <= 0 || h <= 0) {
        if (rgba)
            stbi_image_free(rgba);
        return 0;
    }
    sy_log("art_src", w, h);
    bmp = square_thumb(rgba, w, h, box);
    stbi_image_free(rgba);
    if (!bmp)
        return 0;
    out->bmp = bmp;
    out->w = box;
    out->h = box;
    return 1;
}

int art_from_file(const char *path, int box, SyArt *out)
{
    HANDLE h;
    DWORD size, got = 0;
    u8 *buf;
    int ok;

    memset(out, 0, sizeof(*out));
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    size = (DWORD)GetFileSize(h, NULL);
    if (size == 0 || size > 20u * 1024u * 1024u) {
        CloseHandle(h);
        return 0;
    }
    buf = (u8 *)sy_alloc(size);
    if (!buf) {
        CloseHandle(h);
        return 0;
    }
    if (!ReadFile(h, buf, size, &got, NULL) || got != size) {
        sy_free(buf);
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    ok = art_from_memory(buf, (int)size, box, out);
    sy_free(buf);
    return ok;
}

void art_free(SyArt *a)
{
    if (a->bmp)
        DeleteObject(a->bmp);
    a->bmp = 0;
    a->w = 0;
    a->h = 0;
}



/* ------------------------------------------------------------------ */
/*  thumbnail cache                                                    */
/* ------------------------------------------------------------------ */

static char g_cache_dir[SY_PATH];

void art_set_cache(const char *dir)
{
    g_cache_dir[0] = 0;
    if (!dir || !dir[0])
        return;
    strncpy(g_cache_dir, dir, SY_PATH - 1);
    g_cache_dir[SY_PATH - 1] = 0;
    CreateDirectoryA(g_cache_dir, NULL);
}

static unsigned cache_hash(const char *key)
{
    unsigned h = 2166136261u;
    while (*key) {
        h ^= (unsigned char)*key++;
        h *= 16777619u;
    }
    return h;
}

static void cache_path(const char *key, char *out, int cap)
{
    unsigned h = cache_hash(key);
    static const char hex[] = "0123456789abcdef";
    int i, n = 0;

    out[0] = 0;
    if (!g_cache_dir[0])
        return;
    strncat(out, g_cache_dir, (size_t)(cap - 1));
    strncat(out, "\\a", (size_t)(cap - 1 - strlen(out)));
    for (i = 28; i >= 0; i -= 4) {
        char c = hex[(h >> i) & 0xF];
        out[strlen(out)] = c;
        out[strlen(out) + 1] = 0;
        n++;
    }
    strncat(out, ".raw", (size_t)(cap - 1 - strlen(out)));
}

static int cache_load(const char *key, int box, SyArt *out)
{
    char path[SY_PATH];
    HANDLE h;
    DWORD rd;
    u32 magic = 0;
    int w = 0, hgt = 0;
    u8 *buf;
    BITMAPINFO bi;
    HDC dc;
    void *bits = 0;
    HBITMAP bmp;

    if (!key || !key[0] || !g_cache_dir[0])
        return 0;
    cache_path(key, path, sizeof(path));
    if (!path[0])
        return 0;
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!ReadFile(h, &magic, 4, &rd, NULL) || rd != 4 ||
        !ReadFile(h, &w, 4, &rd, NULL) || rd != 4 ||
        !ReadFile(h, &hgt, 4, &rd, NULL) || rd != 4 ||
        magic != 0x31415953u || w <= 0 || hgt <= 0 || w != box || hgt != box ||
        w > 2048 || hgt > 2048) {
        CloseHandle(h);
        return 0;
    }
    buf = (u8 *)sy_alloc((u32)w * hgt * 4);
    if (!buf) {
        CloseHandle(h);
        return 0;
    }
    if (!ReadFile(h, buf, (DWORD)(w * hgt * 4), &rd, NULL) || rd != (DWORD)(w * hgt * 4)) {
        sy_free(buf);
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hgt;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dc = GetDC(NULL);
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!bmp || !bits) {
        if (bmp)
            DeleteObject(bmp);
        sy_free(buf);
        return 0;
    }
    memcpy(bits, buf, (size_t)(w * hgt * 4));
    sy_free(buf);
    out->bmp = bmp;
    out->w = w;
    out->h = hgt;
    return 1;
}

static void cache_save(const char *key, const SyArt *art)
{
    char path[SY_PATH];
    HANDLE h;
    DWORD wr;
    u32 magic = 0x31415953u;
    int w = art->w, hgt = art->h;
    u8 *buf;
    BITMAPINFO bi;
    HDC dc;

    if (!key || !key[0] || !g_cache_dir[0] || w <= 0 || hgt <= 0)
        return;
    cache_path(key, path, sizeof(path));
    if (!path[0])
        return;
    buf = (u8 *)sy_alloc((u32)w * hgt * 4);
    if (!buf)
        return;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hgt;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dc = GetDC(NULL);
    GetDIBits(dc, art->bmp, 0, (UINT)hgt, buf, &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);

    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, &magic, 4, &wr, NULL);
        WriteFile(h, &w, 4, &wr, NULL);
        WriteFile(h, &hgt, 4, &wr, NULL);
        WriteFile(h, buf, (DWORD)(w * hgt * 4), &wr, NULL);
        CloseHandle(h);
    }
    sy_free(buf);
}

/* Cached variants: the key identifies the source (path + size), so changing
 * the file invalidates the thumbnail. */
int art_from_memory_cached(const char *key, const u8 *data, int len, int box, SyArt *out)
{
    if (cache_load(key, box, out))
        return 1;
    if (!art_from_memory(data, len, box, out))
        return 0;
    cache_save(key, out);
    return 1;
}

int art_from_file_cached(const char *key, const char *path, int box, SyArt *out)
{
    if (cache_load(key, box, out))
        return 1;
    if (!art_from_file(path, box, out))
        return 0;
    cache_save(key, out);
    return 1;
}
