/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* lrc.c -- .lrc lyrics: parse, and answer "which line is playing now".
 *
 * A lyric file next to the track (same base name, .lrc) is loaded when the
 * track is selected.  Encodings seen in the wild are UTF-8 (with or without
 * BOM) and the local ANSI code page (GBK on Chinese Windows 95), so both are
 * accepted.  Storage is static: no allocation, no fragmentation.
 */
#include "shiyin.h"

#define LRC_MAX  400
#define LRC_LINE 96
#define LRC_STAMPS 8

typedef struct {
    int          ms;
    wchar_t     *text;
} LrcLine;

static LrcLine g_lines[LRC_MAX];
static int     g_count;
static wchar_t g_pool[LRC_MAX * LRC_LINE];
static int     g_pool_used;
static int     g_offset;
static char    g_loaded[SY_PATH];

void lrc_clear(void)
{
    g_count = 0;
    g_pool_used = 0;
    g_offset = 0;
    g_loaded[0] = 0;
}

static wchar_t *pool_take(void)
{
    wchar_t *p;
    if (g_pool_used + LRC_LINE > (int)(sizeof(g_pool) / sizeof(g_pool[0])))
        return 0;
    p = &g_pool[g_pool_used];
    g_pool_used += LRC_LINE;
    p[0] = 0;
    return p;
}

static int parse_stamp(const wchar_t **pp, int *ms)
{
    const wchar_t *p = *pp;
    int m = 0, s = 0, f = 0, digits;

    if (*p != '[')
        return 0;
    p++;
    digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 3) { m = m * 10 + (*p - '0'); p++; digits++; }
    if (*p != ':' || digits == 0)
        return 0;
    p++;
    digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 2) { s = s * 10 + (*p - '0'); p++; digits++; }
    if (digits == 0)
        return 0;
    if (*p == '.' || *p == ':') {
        int scale = 10;   /* tenths, then hundredths */
        p++;
        digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 3) {
            f += (*p - '0') * scale;
            scale /= 10;
            p++;
            digits++;
        }
    }
    if (*p != ']')
        return 0;
    p++;
    *ms = m * 60000 + s * 1000 + f * 10;
    *pp = p;
    return 1;
}

static void add_line(int ms, const wchar_t *text)
{
    int i;
    wchar_t *p;

    if (g_count >= LRC_MAX || !text[0])
        return;
    if (ms < 0)
        ms = 0;
    for (i = g_count; i > 0 && g_lines[i - 1].ms > ms; i--)
        g_lines[i] = g_lines[i - 1];
    p = pool_take();
    if (!p)
        return;
    w_copy(p, text, LRC_LINE);
    g_lines[i].ms = ms;
    g_lines[i].text = p;
    g_count++;
}

static int decode(const char *src, int bytes, wchar_t *dst, int cap)
{
    int n;

    if (bytes >= 3 && (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) {
        n = w_from_utf8(src + 3, bytes - 3, dst, cap);
        if (n > 0)
            return n;
    }
    n = MultiByteToWideChar(CP_UTF8, 0, src, bytes, dst, cap - 1);
    if (n > 0)
        return n;
    return MultiByteToWideChar(CP_ACP, 0, src, bytes, dst, cap - 1);
}

int lrc_load(const char *audio_path)
{
    char path[SY_PATH];
    SyFile f;
    static wchar_t wide[16384];
    int n, i, pos, len, dot;
    const wchar_t *p;

    if (!audio_path || !audio_path[0])
        return 0;
    if (!strcmp(g_loaded, audio_path))
        return g_count;

    lrc_clear();
    strncpy(g_loaded, audio_path, SY_PATH - 1);
    g_loaded[SY_PATH - 1] = 0;

    len = (int)strlen(audio_path);
    dot = len;
    while (dot > 0 && audio_path[dot - 1] != '.')
        dot--;
    if (dot <= 0 || len - dot > 4)
        return 0;
    memcpy(path, audio_path, (size_t)(dot - 1));
    strcpy(path + dot - 1, ".lrc");
    if (!file_exists(path))
        return 0;
    if (!file_map(path, &f))
        return 0;
    n = decode((const char *)f.base, (int)f.size, wide, (int)(sizeof(wide) / sizeof(wide[0])));
    file_unmap(&f);
    if (n <= 0)
        return 0;
    wide[n] = 0;

    p = wide;
    while (*p) {
        int stamps[LRC_STAMPS], nst = 0, ms;
        wchar_t text[LRC_LINE];
        int tlen = 0;

        while (*p == ' ' || *p == '\t')
            p++;
        while (nst < LRC_STAMPS && parse_stamp(&p, &ms))
            stamps[nst++] = ms;
        while (*p == ' ' || *p == '\t')
            p++;
        while (*p && *p != '\r' && *p != '\n' && tlen < LRC_LINE - 1)
            text[tlen++] = *p++;
        text[tlen] = 0;
        while (tlen > 0 && (text[tlen - 1] == ' ' || text[tlen - 1] == '\t'))
            text[--tlen] = 0;

        if (nst > 0) {
            int k;
            for (k = 0; k < nst; k++)
                add_line(stamps[k] + g_offset, text);
        } else if (tlen > 8 && text[0] == '[' && text[1] == 'o') {
            /* [offset:500] */
            int k = 0;
            while (text[k] && text[k] != ':')
                k++;
            if (text[k] == ':') {
                int sign = 1, v = 0;
                k++;
                if (text[k] == '-') { sign = -1; k++; }
                else if (text[k] == '+') k++;
                while (text[k] >= '0' && text[k] <= '9') { v = v * 10 + (text[k] - '0'); k++; }
                g_offset = sign * v;
            }
        }
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }

    /* drop the empty lines that metadata tags leave behind */
    pos = 0;
    for (i = 0; i < g_count; i++) {
        if (g_lines[i].text && g_lines[i].text[0])
            g_lines[pos++] = g_lines[i];
    }
    g_count = pos;
    return g_count;
}

int lrc_count(void)
{
    return g_count;
}

const wchar_t *lrc_text(int i)
{
    if (i < 0 || i >= g_count)
        return L"";
    return g_lines[i].text;
}

int lrc_ms(int i)
{
    if (i < 0 || i >= g_count)
        return 0;
    return g_lines[i].ms;
}

int lrc_index_at(int ms)
{
    int i, cur = -1;
    for (i = 0; i < g_count; i++) {
        if (g_lines[i].ms <= ms)
            cur = i;
        else
            break;
    }
    return cur;
}
