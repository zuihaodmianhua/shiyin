/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* main.c -- Shiyin window, layout, painting and input. */
#include "shiyin.h"

double pow(double, double);
COLORREF C_BG;
COLORREF C_PANEL;
COLORREF C_CARD;
COLORREF C_LINE;
COLORREF C_LINE_SOFT;
COLORREF C_TEXT;
COLORREF C_TEXT2;
COLORREF C_TEXT3;
COLORREF C_ACCENT;
COLORREF C_ACCENT_D;
COLORREF C_HOVER;
COLORREF C_SELECT;

/* 0 = dark, 1 = light, 2 = follow the clock */
static int g_theme;

static int g_light;
static DWORD g_theme_check;

static void theme_apply(void)
{
    int light = 0;
    if (g_theme == 1)
        light = 1;
    else if (g_theme == 2) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        light = (st.wHour >= 7 && st.wHour < 19);
    }
    g_light = light;
    if (light) {
        C_BG = RGB(0xF6, 0xF7, 0xF9);
        C_PANEL = RGB(0xEC, 0xEE, 0xF2);
        C_CARD = RGB(0xE4, 0xE7, 0xEC);
        C_LINE = RGB(0xD3, 0xD7, 0xDE);
        C_LINE_SOFT = RGB(0xE2, 0xE5, 0xEA);
        C_TEXT = RGB(0x1B, 0x1E, 0x24);
        C_TEXT2 = RGB(0x4C, 0x53, 0x5E);
        C_TEXT3 = RGB(0x86, 0x8D, 0x99);
        C_ACCENT = RGB(0x14, 0x9E, 0x76);
        C_ACCENT_D = RGB(0x0E, 0x7A, 0x5A);
        C_HOVER = RGB(0xE8, 0xEB, 0xF0);
        C_SELECT = RGB(0xDD, 0xE2, 0xEA);
    } else {
        C_BG = RGB(0x0F, 0x10, 0x13);
        C_PANEL = RGB(0x15, 0x17, 0x1B);
        C_CARD = RGB(0x1A, 0x1D, 0x23);
        C_LINE = RGB(0x24, 0x28, 0x30);
        C_LINE_SOFT = RGB(0x1E, 0x22, 0x29);
        C_TEXT = RGB(0xED, 0xEE, 0xF0);
        C_TEXT2 = RGB(0x9A, 0xA0, 0xAB);
        C_TEXT3 = RGB(0x63, 0x69, 0x75);
        C_ACCENT = RGB(0x4A, 0xD9, 0xA5);
        C_ACCENT_D = RGB(0x2E, 0xA8, 0x7C);
        C_HOVER = RGB(0x1D, 0x21, 0x28);
        C_SELECT = RGB(0x21, 0x26, 0x2E);
    }
}


static int g_gapless_on = 1;
static void apply_title_theme(void);
static RECT g_theme_row;
static RECT g_lang_row;

static int  recent_get(const char *path);
static int  replaygain_db100(const char *path);
static int  next_index_for(int idx);
static void recent_set(const char *path, int ms);

#define ROW_H      52
#define BAR_H      96
#define LEFT_W     310
#define TOP_H      64

#define HOV_NONE   (-1000)
#define HOV_ADDF   (-1)
#define HOV_FOLDER (-2)
#define HOV_GEAR   (-3)
#define HOV_MODE   (-3)
#define HOV_LYR    (-4)
#define HOV_GLYPH(g) (-100 - (g))

/* Playback mode: exactly one of these is active at any time.  Keeping the
 * queue order and the end-of-queue behaviour in a single state removes the
 * "shuffle + repeat are both lit, what now?" ambiguity. */
#define MODE_ORDER   0     /* 顺序播放：播到最后就停 */
#define MODE_ALL     1     /* 列表循环：播完回到第一首 */
#define MODE_ONE     2     /* 单曲循环：一直重复当前这首 */
#define MODE_SHUFFLE 3     /* 随机播放：结束后随机选下一首 */
#define MODE_COUNT   4

typedef struct {
    RECT top, left, list, search, addf, addfolder, gear, back, lyr;
    RECT hdr_title, hdr_album, hdr_time;
    RECT seek, vol, prev, play, next, mode, empty_btn;
    RECT cover, info;
    int  rows_top;
    int  view_h;
    int  row_h;
} Layout;

static HINSTANCE g_inst;
static HWND      g_hwnd;
static SyList    g_list;
static int      *g_view;
static int       g_view_n, g_view_cap;
static int       g_sel = -1;
static int       g_playing = -1;
static int       g_hover = HOV_NONE;
static int       g_scroll;

static int       g_dirty;
static unsigned char *g_mark;        /* per track multi-selection */
static int       g_mark_cap;
static int       g_anchor = -1;
static int       g_press_row = -1;
static int       g_press_y;
static int       g_drag_row = -1;      /* track being dragged */
static int       g_drop_pos = -1;
static int       g_drag_y;
static int       row_shift(int i);
static int       ease_shift(void);
static int       g_anim_t;             /* 0..256 easing progress for row shifts */
static int       g_from_pos = -1;      /* insertion slot in view coordinates */
static int       g_lyrics;
static int       g_sort;             /* 0 manual, 1 title, 2 artist, 3 album, 4 length */
static int       g_sort_desc;            /* settings need writing */
static int       g_resume_idx = -1;  /* track whose position we remember */
static int       g_resume_ms;
static DWORD     g_save_at;
static DWORD     g_pos_at;
static char      g_search[128];
static int       g_search_on;
static int       g_caret;
static int       g_anim;
static int       g_vol = 70;
static int       g_last_vol = 70;
static int       g_mode = MODE_ORDER;
static int       g_paused, g_active, g_pos, g_dur;
static SyArt     g_art;
static int       g_art_idx = -1;
static wchar_t   g_toast[192];
static DWORD     g_toast_at;
static Layout    L;
static char      g_ini[SY_PATH];
static int       g_drag;
static UINT      g_msg_wheel;
static HDC       g_mdc;
static HBITMAP   g_mbmp;
static int       g_mw, g_mh;
static int       g_dark_tried;
static u32       g_seed;

#define ID_TIMER 1

/* ------------------------------------------------------------------ */
/*  helpers                                                            */
/* ------------------------------------------------------------------ */

static void fmt_time(int ms, wchar_t *buf, int cap)
{
    int total, s, m, h;
    wchar_t tmp[16];
    if (ms < 0)
        ms = 0;
    total = ms / 1000;
    s = total % 60;
    m = (total / 60) % 60;
    h = total / 3600;
    buf[0] = 0;
    if (h > 0) {
        w_from_int(h, tmp, 16);
        w_copy(buf, tmp, cap);
        w_cat(buf, L":", cap);
        if (m < 10)
            w_cat(buf, L"0", cap);
    }
    w_from_int(m, tmp, 16);
    w_cat(buf, tmp, cap);
    w_cat(buf, L":", cap);
    if (s < 10)
        w_cat(buf, L"0", cap);
    w_from_int(s, tmp, 16);
    w_cat(buf, tmp, cap);
}

static void fmt_size(unsigned int bytes, wchar_t *buf, int cap)
{
    wchar_t a[16];
    if (bytes >= 1024u * 1024u) {
        w_from_int((int)(bytes / (1024u * 1024u)), a, 16);
        w_copy(buf, a, cap);
        w_cat(buf, L".", cap);
        w_from_int((int)((bytes / (1024u * 102u)) % 10), a, 16);
        w_cat(buf, a, cap);
        w_cat(buf, L" MB", cap);
    } else if (bytes >= 1024u) {
        w_from_int((int)(bytes / 1024u), a, 16);
        w_copy(buf, a, cap);
        w_cat(buf, L" KB", cap);
    } else {
        w_from_int((int)bytes, a, 16);
        w_copy(buf, a, cap);
        w_cat(buf, L" B", cap);
    }
}

static int hit(const RECT *r, int x, int y)
{
    return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

static void toast(const wchar_t *msg)
{
    w_copy(g_toast, msg, 192);
    g_toast_at = GetTickCount();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static int view_pos_of(int track);

static void invalidate_all(void)
{
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/* Repainting the whole window several times per second is fine on NT, but on
 * a Windows 95 machine it eats the UI thread.  While playing we only repaint
 * the transport bar and the row with the equaliser animation. */
static void invalidate_bar(void)
{
    RECT r;
    if (!g_hwnd || g_mh <= 0)
        return;
    r.left = 0;
    r.right = g_mw;
    r.top = g_mh - S(BAR_H);
    r.bottom = g_mh;
    InvalidateRect(g_hwnd, &r, FALSE);
}

static void invalidate_playing_row(void)
{
    RECT r;
    int p;
    if (g_playing < 0 || g_hwnd == NULL)
        return;
    p = view_pos_of(g_playing);
    if (p < 0)
        return;
    r.left = L.list.left - S(10);
    r.right = L.list.right + S(12);
    r.top = L.rows_top + p * L.row_h - g_scroll;
    r.bottom = r.top + L.row_h;
    if (r.bottom < L.rows_top || r.top > L.list.bottom)
        return;
    InvalidateRect(g_hwnd, &r, FALSE);
}

/* ------------------------------------------------------------------ */
/*  view                                                               */
/* ------------------------------------------------------------------ */

static int matches(const SyTrack *t)
{
    wchar_t q[128];
    if (!g_search[0])
        return 1;
    w_from_ansi(g_search, q, 128);
    if (!q[0])
        return 1;
    if (w_contains(t->title, q) || w_contains(t->artist, q) || w_contains(t->album, q))
        return 1;
    return 0;
}

static void sync_art(void);
static int  view_pos_of(int track);

static void rebuild_view(void)
{
    int i, content, maxs;
    g_view_n = 0;
    for (i = 0; i < g_list.count; i++) {
        if (!matches(&g_list.items[i]))
            continue;
        if (g_view_n >= g_view_cap) {
            int nc = g_view_cap ? g_view_cap * 2 : 128;
            int *nv = (int *)sy_realloc(g_view, (u32)nc * sizeof(int));
            if (!nv)
                return;
            g_view = nv;
            g_view_cap = nc;
        }
        g_view[g_view_n++] = i;
    }
    if (g_sel >= g_list.count)
        g_sel = g_list.count - 1;
    if (g_view_n > 0 && view_pos_of(g_sel) < 0) {
        g_sel = g_view[0];
        sync_art();
    }
    content = g_view_n * S(ROW_H);
    maxs = content - L.view_h;
    if (maxs < 0)
        maxs = 0;
    if (g_scroll > maxs)
        g_scroll = maxs;
    if (g_scroll < 0)
        g_scroll = 0;
}

static int view_pos_of(int track)
{
    int i;
    for (i = 0; i < g_view_n; i++)
        if (g_view[i] == track)
            return i;
    return -1;
}

static void ensure_visible(int track)
{
    if (L.view_h <= 0 || L.row_h <= 0)
        return;                       /* window not laid out yet */
    int p = view_pos_of(track);
    int top, bottom;
    if (p < 0)
        return;
    top = p * S(ROW_H);
    bottom = top + S(ROW_H);
    if (top < g_scroll)
        g_scroll = top;
    if (bottom > g_scroll + L.view_h)
        g_scroll = bottom - L.view_h;
    if (g_scroll < 0)
        g_scroll = 0;
}

/* ------------------------------------------------------------------ */
/*  artwork                                                            */
/* ------------------------------------------------------------------ */

static DWORD g_art_t0;

static void load_art(int track)
{
    static const char *names[] = {
        "cover.jpg", "folder.jpg", "front.jpg", "album.jpg",
        "cover.jpeg", "folder.jpeg", "cover.png", "folder.png", "front.png", 0
    };
    u8 *data = 0;
    int len = 0;
    int box = S(LEFT_W - 56);

    if (g_art_idx == track)
        return;
    art_free(&g_art);
    g_art_idx = track;
    g_art_t0 = GetTickCount();
    if (track < 0 || track >= g_list.count)
        return;
    {
        /* cache key: file identity, so a re-tagged track re-decodes once */
        char key[SY_PATH + 24];
        wchar_t num[16];
        char anum[24];
        w_copy(num, L"", 16);
        w_from_int((int)g_list.items[track].file_size, num, 16);
        w_ansi(num, anum, sizeof(anum));
        key[0] = 0;
        strncat(key, g_list.items[track].path, sizeof(key) - 1);
        strncat(key, "|", sizeof(key) - strlen(key) - 1);
        strncat(key, anum, sizeof(key) - strlen(key) - 1);

    if (tags_cover(g_list.items[track].path, &data, &len)) {
        art_from_memory_cached(key, data, len, box, &g_art);
        sy_free(data);
        if (g_art.bmp) {
            sy_log("art", (int)(GetTickCount() - g_art_t0), 1);
            return;
        }
    }
    {
        char dir[SY_PATH], full[SY_PATH];
        int i;
        path_dir(g_list.items[track].path, dir, sizeof(dir));
        for (i = 0; names[i]; i++) {
            path_join(full, sizeof(full), dir, names[i]);
            if (file_exists(full) && art_from_file_cached(key, full, box, &g_art))
                return;
        }
    }
    }
    sy_log("art", (int)(GetTickCount() - g_art_t0), g_art.bmp ? 1 : 0);
}

/* keep the artwork in sync with whatever the info panel is showing */
static void sync_lyrics(void)
{
    if (g_sel >= 0 && g_sel < g_list.count)
        lrc_load(g_list.items[g_sel].path);
    else
        lrc_clear();
}

static void sync_art(void)
{
    int show = (g_playing >= 0 && g_playing < g_list.count) ? g_playing : g_sel;
    if (show != g_art_idx)
        load_art(show);
}

/* ------------------------------------------------------------------ */
/*  playback                                                           */
/* ------------------------------------------------------------------ */

static int rand_index(void)
{
    int n = g_list.count;
    if (!g_seed)
        g_seed = GetTickCount() | 1;
    if (n <= 1)
        return 0;
    g_seed = g_seed * 1103515245u + 12345u;
    return (int)((g_seed >> 9) % (u32)n);
}

static void play_track(int idx)
{
    if (idx < 0 || idx >= g_list.count)
        return;
    g_sel = idx;
    audio_set_volume(g_vol);
    {
        int db = replaygain_db100(g_list.items[idx].path);
        int mult = 256;
        if (db != 0) {
            double m = pow(10.0, (double)db / 2000.0) * 256.0;
            mult = (int)(m + 0.5);
            if (mult < 8) mult = 8;
            if (mult > 1024) mult = 1024;
        }
        audio_set_replaygain(mult);
        sy_log("rg", db, mult);
    }
    {
    int start = 0;
    if (idx == g_resume_idx && g_resume_ms > 0)
        start = g_resume_ms;
    else {
        int saved = recent_get(g_list.items[idx].path);
        if (saved > 3000 && g_list.items[idx].duration_ms > 0 &&
            saved < g_list.items[idx].duration_ms - 5000)
            start = saved;
    }
    if (!audio_play(g_list.items[idx].path, start)) {
        /* the audio worker never started: say so instead of pretending */
        toast(L"音频引擎未能启动（无法创建播放线程）");
        sy_log("play_no_engine", idx, 0);
        invalidate_all();
        return;
    }
    g_playing = idx;
    g_active = 1;
    g_paused = 0;
    g_pos = start;
    g_dur = g_list.items[idx].duration_ms;
    if (g_gapless_on) {
        int nx = next_index_for(idx);
        audio_set_next(nx >= 0 ? g_list.items[nx].path : "");
    } else {
        audio_set_next("");
    }
    if (idx == g_resume_idx)
        g_resume_ms = 0;
    g_dirty = 1;
    }
    load_art(idx);
    ensure_visible(idx);
    invalidate_all();
}

static void stop_playback(void)
{
    audio_stop();
    g_active = 0;
    g_paused = 0;
    g_playing = -1;
    g_pos = 0;
    sync_art();
    invalidate_all();
}

static int random_other(void)
{
    int n = g_list.count;
    int cur = g_playing >= 0 ? g_playing : g_sel;
    int idx, guard = 0;
    if (n <= 1)
        return 0;
    do {
        idx = rand_index();
    } while (idx == cur && ++guard < 16);
    return idx;
}

static const wchar_t *mode_name(int m)
{
    switch (m) {
    case MODE_ALL:     return L"列表循环";
    case MODE_ONE:     return L"单曲循环";
    case MODE_SHUFFLE: return L"随机播放";
    default:           return L"顺序播放";
    }
}

static int mode_glyph(int m)
{
    switch (m) {
    case MODE_ALL:     return GLYPH_REPEAT;
    case MODE_ONE:     return GLYPH_REPEAT1;
    case MODE_SHUFFLE: return GLYPH_SHUFFLE;
    default:           return GLYPH_ORDER;
    }
}

static void cycle_mode(void)
{
    g_mode = (g_mode + 1) % MODE_COUNT;
    toast(mode_name(g_mode));
    invalidate_all();
}

/* auto_advance: 1 when the current track simply finished, 0 for a manual click. */
static void next_track(int auto_advance)
{
    int p, next;
    if (g_list.count == 0 || g_view_n == 0)
        return;
    if (g_mode == MODE_SHUFFLE) {
        play_track(random_other());
        return;
    }
    p = view_pos_of(g_playing >= 0 ? g_playing : g_sel);
    next = (p < 0) ? 0 : p + 1;
    if (next >= g_view_n) {
        if (!auto_advance || g_mode == MODE_ALL) {
            next = 0;                       /* 手动下一首总是绕回开头 */
        } else {
            toast(L"列表播放完毕");
            stop_playback();                /* 顺序播放：到最后一首就停 */
            return;
        }
    }
    play_track(g_view[next]);
}

static void prev_track(void)
{
    int p, prev;
    if (g_list.count == 0 || g_view_n == 0)
        return;
    if (g_mode == MODE_SHUFFLE) {
        play_track(random_other());
        return;
    }
    p = view_pos_of(g_playing >= 0 ? g_playing : g_sel);
    prev = (p <= 0) ? g_view_n - 1 : p - 1;
    play_track(g_view[prev]);
}

static void toggle_play(void)
{
    if (!g_active) {
        int idx = g_sel >= 0 ? g_sel : (g_view_n > 0 ? g_view[0] : -1);
        if (idx >= 0)
            play_track(idx);
        return;
    }
    audio_toggle_pause();
    g_paused = audio_paused();
    invalidate_all();
}

static void seek_to(int ms)
{
    if (!g_active)
        return;
    if (ms < 0)
        ms = 0;
    if (g_dur > 0 && ms > g_dur)
        ms = g_dur;
    g_pos = ms;
    audio_seek(ms);
    invalidate_all();
}

static void set_volume(int v)
{
    if (v < 0)
        v = 0;
    if (v > 100)
        v = 100;
    g_vol = v;
    audio_set_volume(v);
    invalidate_all();
}

/* ------------------------------------------------------------------ */
/*  list editing                                                       */
/* ------------------------------------------------------------------ */

/* formats we knowingly do not decode yet: say so instead of ignoring the file */
static int unsupported_ext(const char *path)
{
    static const char *exts[] = {
        "m4a", "m4b", "mp4", "aac", "wma", "asf", "ogg", "oga", "opus",
        "ape", "wv", "dsf", "dff", "mid", "midi", 0
    };
    int i;
    for (i = 0; exts[i]; i++)
        if (ext_is(path, exts[i]))
            return 1;
    return 0;
}

static int add_path(const char *path)
{
    int before = g_list.count;
    int added;
    if (is_dir(path))
        added = list_add_tree(&g_list, path, 0);
    else
        added = list_add(&g_list, path);
    if (added) {
        wchar_t msg[64], num[16];
        w_from_int(added, num, 16);
        w_copy(msg, (g_lang ? L"Added " : L"已添加 "), 64);
        w_cat(msg, num, 64);
        w_cat(msg, g_lang ? L" songs" : L" 首歌曲", 64);
        toast(msg);
    }
    if (g_list.count > before) {
        if (added == 1 && before < g_list.count)
            sy_log_str("add", g_list.items[g_list.count - 1].path);
        rebuild_view();
        if (g_sel < 0)
            g_sel = g_list.count - 1;
        ensure_visible(before);
        invalidate_all();
    } else if (!added) {
        if (unsupported_ext(path))
            toast(L"暂不支持该格式（AAC/M4A/WMA/OGG），请先转换为 FLAC 或 MP3");
        else
            toast(L"没有找到可播放的音频文件");
    }
    return added;
}

static void mark_clear(void);

static int w_cmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

static const wchar_t *sort_key(const SyTrack *t, int mode)
{
    switch (mode) {
    case 1: return t->title[0] ? t->title : L"";
    case 2: return t->artist;
    case 3: return t->album;
    default: return t->title;
    }
}

/* Reorder the playlist itself so the order survives a restart. */
static void sort_list(void)
{
    int n = g_list.count, i, j;
    int *idx;
    SyTrack *tmp;

    if (n < 2 || g_sort == 0) {
        rebuild_view();
        return;
    }
    idx = (int *)sy_alloc((u32)n * sizeof(int));
    tmp = (SyTrack *)sy_alloc((u32)n * sizeof(SyTrack));
    if (!idx || !tmp) {
        sy_free(idx);
        sy_free(tmp);
        return;
    }
    for (i = 0; i < n; i++)
        idx[i] = i;

    for (i = 1; i < n; i++) {
        int v = idx[i];
        j = i - 1;
        while (j >= 0) {
            const SyTrack *a = &g_list.items[idx[j]];
            const SyTrack *b = &g_list.items[v];
            int c;
            if (g_sort == 4)
                c = a->duration_ms - b->duration_ms;
            else
                c = w_cmp(sort_key(a, g_sort), sort_key(b, g_sort));
            if (g_sort_desc)
                c = -c;
            if (c <= 0)
                break;
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = v;
    }

    {
        int playing = g_playing, sel = g_sel;
        for (i = 0; i < n; i++)
            tmp[i] = g_list.items[idx[i]];
        memcpy(g_list.items, tmp, (size_t)n * sizeof(SyTrack));
        for (i = 0; i < n; i++) {
            if (idx[i] == playing) g_playing = i;
            if (idx[i] == sel) g_sel = i;
        }
        mark_clear();
    }
    sy_free(idx);
    sy_free(tmp);
    rebuild_view();
}

static void set_sort(int mode)
{
    if (g_sort == mode)
        g_sort_desc = !g_sort_desc;
    else {
        g_sort = mode;
        g_sort_desc = 0;
    }
    if (g_sort == 0)
        g_sort_desc = 0;
    sort_list();
    g_dirty = 1;
    invalidate_all();
}

static void mark_ensure(void);

/* Move the track at list index "from" so that it lands at "to". */
static void move_track(int from, int to)
{
    SyTrack tmp;
    int i;

    if (from < 0 || from >= g_list.count)
        return;
    if (to < 0)
        to = 0;
    if (to > g_list.count - 1)
        to = g_list.count - 1;
    if (from == to)
        return;

    tmp = g_list.items[from];
    if (from < to) {
        for (i = from; i < to; i++)
            g_list.items[i] = g_list.items[i + 1];
    } else {
        for (i = from; i > to; i--)
            g_list.items[i] = g_list.items[i - 1];
    }
    g_list.items[to] = tmp;

    if (g_playing == from) g_playing = to;
    else if (from < g_playing && to >= g_playing) g_playing--;
    else if (from > g_playing && to <= g_playing) g_playing++;
    if (g_sel == from) g_sel = to;

    g_dirty = 1;
    rebuild_view();
    ensure_visible(g_sel);
}

static void mark_ensure(void)
{
    int need = g_list.count + 8;
    if (g_mark_cap >= need)
        return;
    {
        int nc = need + 32;
        unsigned char *p = (unsigned char *)sy_realloc(g_mark, (u32)nc);
        if (!p)
            return;
        memset(p + g_mark_cap, 0, (size_t)(nc - g_mark_cap));
        g_mark = p;
        g_mark_cap = nc;
    }
}

static void mark_clear(void)
{
    if (g_mark)
        memset(g_mark, 0, (size_t)g_mark_cap);
}

static int mark_count(void)
{
    int i, n = 0;
    for (i = 0; i < g_list.count; i++)
        if (g_mark && g_mark[i])
            n++;
    return n;
}

static void mark_only(int idx)
{
    mark_ensure();
    mark_clear();
    if (idx >= 0 && idx < g_list.count)
        g_mark[idx] = 1;
}

/* Move every marked track one slot up (-1) or down (+1), keeping their order. */
static void move_marked(int dir)
{
    int i;
    if (g_sort != 0) {
        toast(L"拖动/移动前请先按“手动”排序");
        return;
    }
    mark_ensure();
    if (dir < 0) {
        for (i = 1; i < g_list.count; i++) {
            if (g_mark[i] && !g_mark[i - 1]) {
                SyTrack t = g_list.items[i];
                int k;
                g_list.items[i] = g_list.items[i - 1];
                g_list.items[i - 1] = t;
                g_mark[i] = 0;
                g_mark[i - 1] = 1;
                for (k = 0; k < g_list.count; k++)
                    (void)k;
                if (g_playing == i) g_playing = i - 1;
                else if (g_playing == i - 1) g_playing = i;
                if (g_sel == i) g_sel = i - 1;
                else if (g_sel == i - 1) g_sel = i;
                i++;
            }
        }
    } else {
        for (i = g_list.count - 2; i >= 0; i--) {
            if (g_mark[i] && !g_mark[i + 1]) {
                SyTrack t = g_list.items[i];
                g_list.items[i] = g_list.items[i + 1];
                g_list.items[i + 1] = t;
                g_mark[i] = 0;
                g_mark[i + 1] = 1;
                if (g_playing == i) g_playing = i + 1;
                else if (g_playing == i + 1) g_playing = i;
                if (g_sel == i) g_sel = i + 1;
                else if (g_sel == i + 1) g_sel = i;
                i--;
            }
        }
    }
    g_dirty = 1;
    rebuild_view();
    invalidate_all();
}

static void remove_selected(void)
{
    int i, keep, n = mark_count();
    if (n == 0) {
        if (g_sel < 0 || g_sel >= g_list.count)
            return;
        mark_ensure();
        g_mark[g_sel] = 1;
        n = 1;
    }
    {
        int was_playing = 0;
        /* map old playing track through the compaction */
        int playing_idx = g_playing;
        keep = 0;
        for (i = 0; i < g_list.count; i++) {
            if (g_mark[i]) {
                if (i == playing_idx)
                    was_playing = 1;
                continue;
            }
            if (i != keep)
                g_list.items[keep] = g_list.items[i];
            if (i == playing_idx)
                g_playing = keep;
            keep++;
        }
        g_list.count = keep;
        if (was_playing) {
            stop_playback();
            g_playing = -1;
        }
        if (g_playing >= g_list.count)
            g_playing = -1;
        if (g_sel >= g_list.count)
            g_sel = g_list.count - 1;
        mark_clear();
        mark_ensure();
        g_dirty = 1;
        rebuild_view();
        invalidate_all();
    }
}

/* ------------------------------------------------------------------ */
/*  dialogs                                                            */
/* ------------------------------------------------------------------ */

static void open_files(void)
{
    static char buf[32768];
    static char filter[512];
    static const wchar_t *fparts[] = {
        L"音乐文件 (*.mp3;*.flac;*.wav;*.aiff;*.au)",
        L"*.mp3;*.flac;*.wav;*.aiff;*.aif;*.aifc;*.au;*.snd",
        L"所有文件 (*.*)", L"*.*", 0
    };
    char title[64];
    OPENFILENAMEA ofn;
    int fi;

    {
        char *q = filter;
        for (fi = 0; fparts[fi]; fi++) {
            w_ansi(fparts[fi], q, (int)(sizeof(filter) - (size_t)(q - filter)));
            q += strlen(q) + 1;
        }
        *q = 0;
    }
    w_ansi(L"添加音乐文件", title, sizeof(title));

    /* Must be fully zeroed: the multi-select terminator is detected by the
     * byte after the first NUL, and this buffer is static. */
    memset(buf, 0, sizeof(buf));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = 76;      /* Windows 95 comdlg32 structure size */
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf);
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn))
        return;
    {
        char dir[SY_PATH];
        int n = (int)strlen(buf);
        strncpy(dir, buf, SY_PATH - 1);
        dir[SY_PATH - 1] = 0;
        if (n + 1 < (int)sizeof(buf) && buf[n + 1]) {
            char *p = buf + n + 1;
            while (*p) {
                char full[SY_PATH];
                path_join(full, sizeof(full), dir, p);
                if (file_exists(full))
                    add_path(full);
                p += strlen(p) + 1;
            }
        } else if (file_exists(dir)) {
            add_path(dir);
        }
    }
}

typedef struct {
    HWND  hwndOwner;
    void *pidlRoot;
    LPSTR pszDisplayName;
    LPCSTR lpszTitle;
    UINT  ulFlags;
    void *lpfn;
    LPARAM lParam;
    int   iImage;
} BI95;

static void open_folder(void)
{
    HMODULE sh = LoadLibraryA("shell32.dll");
    HMODULE ol = LoadLibraryA("ole32.dll");
    typedef void *(WINAPI *PFN_BROWSE)(const BI95 *);
    typedef int (WINAPI *PFN_GETPATH)(const void *, LPSTR);
    typedef void (WINAPI *PFN_FREE)(void *);
    PFN_BROWSE fn;
    PFN_GETPATH gp;
    PFN_FREE fr = 0;
    char path[SY_PATH];

    if (!sh) {
        toast(L"无法打开文件夹选择器");
        return;
    }
    fn = (PFN_BROWSE)GetProcAddress(sh, "SHBrowseForFolderA");
    gp = (PFN_GETPATH)GetProcAddress(sh, "SHGetPathFromIDListA");
    if (ol)
        fr = (PFN_FREE)GetProcAddress(ol, "CoTaskMemFree");
    if (!fn || !gp) {
        FreeLibrary(sh);
        if (ol)
            FreeLibrary(ol);
        toast(L"系统不支持文件夹选择，请直接把文件夹拖入窗口");
        return;
    }
    {
        BI95 bi;
        void *pidl;
        memset(&bi, 0, sizeof(bi));
        bi.hwndOwner = g_hwnd;
        bi.lpszTitle = "选择音乐文件夹";
        bi.ulFlags = 0x0001;    /* BIF_RETURNONLYFSDIRS */
        pidl = fn(&bi);
        if (pidl) {
            path[0] = 0;
            if (gp(pidl, path))
                add_path(path);
            if (fr)
                fr(pidl);
        }
    }
    FreeLibrary(sh);
    if (ol)
        FreeLibrary(ol);
}

/* ------------------------------------------------------------------ */
/*  layout                                                             */
/* ------------------------------------------------------------------ */

static void layout(int W, int H)
{
    int pad = S(28);
    int top = S(TOP_H);
    int bar = S(BAR_H);
    int lw = S(LEFT_W);

    memset(&L, 0, sizeof(L));
    L.top.left = 0; L.top.top = 0; L.top.right = W; L.top.bottom = top;

    L.left.left = 0; L.left.top = top; L.left.right = lw; L.left.bottom = H - bar;

    L.cover.left = pad;
    L.cover.top = top + S(22);
    L.cover.right = lw - pad;
    {
        int avail = (H - bar) - L.cover.top;
        int need = S(232);                 /* info block below the cover */
        int maxh = avail - S(26) - need;
        int cs = L.cover.right - L.cover.left;
        if (cs > maxh)
            cs = maxh;
        if (cs < S(132))
            cs = S(132);
        L.cover.right = L.cover.left + cs;
    }
    L.cover.bottom = L.cover.top + (L.cover.right - L.cover.left);

    L.info.left = pad;
    L.info.top = L.cover.bottom + S(26);
    L.info.right = lw - pad;
    L.info.bottom = H - bar - S(16);

    L.list.left = lw + S(4);
    L.list.top = top + S(16);
    L.list.right = W - S(24);
    L.list.bottom = H - bar;

    L.search.left = L.list.left;
    L.search.top = L.list.top;
    L.search.right = L.list.left + S(272);
    L.search.bottom = L.list.top + S(34);

    L.play.left = W / 2 - S(25);
    L.play.top = H - bar + S(44);
    L.play.right = L.play.left + S(50);
    L.play.bottom = L.play.top + S(50);

    L.prev.left = L.play.left - S(58);
    L.prev.top = H - bar + S(52);
    L.prev.right = L.prev.left + S(34);
    L.prev.bottom = L.prev.top + S(34);

    L.next.left = L.play.right + S(24);
    L.next.top = L.prev.top;
    L.next.right = L.next.left + S(34);
    L.next.bottom = L.prev.bottom;

    L.seek.left = pad + S(52);
    L.seek.right = W - pad - S(52);
    L.seek.top = H - bar + S(24);
    L.seek.bottom = L.seek.top + S(20);

    L.vol.right = W - pad;
    L.vol.left = L.vol.right - S(78);
    L.vol.top = H - bar + S(58);
    L.vol.bottom = L.vol.top + S(18);

    {
        HDC dc = GetDC(g_hwnd);
        int w1 = ui_text_w(dc, L"添加文件", -1, ui_font(12, FW_NORMAL)) + S(14) + S(8) + S(34);
        int w2 = ui_text_w(dc, L"文件夹", -1, ui_font(12, FW_NORMAL)) + S(14) + S(8) + S(34);
        int mtw = ui_text_w(dc, L"顺序播放", -1, ui_font(11, FW_NORMAL));
        int mw = S(12) + S(18) + S(8) + mtw + S(12);
        ReleaseDC(g_hwnd, dc);
        if (w1 < S(104))
            w1 = S(104);
        if (w2 < S(92))
            w2 = S(92);
        L.addf.right = W - S(24);
        L.addf.top = S(18);
        L.addf.bottom = L.addf.top + S(33);
        L.addf.left = L.addf.right - w1;
        L.addfolder.right = L.addf.left - S(8);
        L.addfolder.top = L.addf.top;
        L.addfolder.bottom = L.addf.bottom;
        L.addfolder.left = L.addfolder.right - w2;

        L.lyr.right = L.addfolder.left - S(8);
        L.lyr.top = L.addf.top;
        L.lyr.bottom = L.addf.bottom;
        L.lyr.left = L.lyr.right - S(58);

        L.gear.right = L.lyr.left - S(8);
        L.gear.top = L.addf.top;
        L.gear.bottom = L.addf.bottom;
        L.gear.left = L.gear.right - S(34);

        L.back = L.addf;

        L.mode.left = pad;
        L.mode.right = pad + mw;
        L.mode.top = H - bar + S(54);
        L.mode.bottom = L.mode.top + S(30);
    }

    L.rows_top = L.search.bottom + S(40);
    L.view_h = L.list.bottom - L.rows_top - S(8);
    if (L.view_h < 10)
        L.view_h = 10;
    L.row_h = S(ROW_H);

    {
        int ct = L.list.left + S(46);
        int ca = ct + (L.list.right - ct) * 58 / 100;
        L.hdr_title.left = ct;
        L.hdr_title.right = ca - S(10);
        L.hdr_album.left = ca;
        L.hdr_album.right = L.list.right - S(56);
        L.hdr_time.left = L.list.right - S(52);
        L.hdr_time.right = L.list.right;
        L.hdr_title.top = L.rows_top - S(34);
        L.hdr_title.bottom = L.rows_top - S(2);
        L.hdr_album.top = L.hdr_title.top;
        L.hdr_album.bottom = L.hdr_title.bottom;
        L.hdr_time.top = L.hdr_title.top;
        L.hdr_time.bottom = L.hdr_title.bottom;
    }



    L.empty_btn.left = (L.list.left + L.list.right) / 2 - S(68);
    L.empty_btn.right = L.empty_btn.left + S(136);
    L.empty_btn.top = L.rows_top + L.view_h / 2 + S(36);
    L.empty_btn.bottom = L.empty_btn.top + S(36);
}

/* ------------------------------------------------------------------ */
/*  painting                                                           */
/* ------------------------------------------------------------------ */

static void draw_cover(HDC dc)
{
    RECT r = L.cover;
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0)
        return;
    if (g_art.bmp) {
        HDC mdc = CreateCompatibleDC(dc);
        HBITMAP old = (HBITMAP)SelectObject(mdc, g_art.bmp);
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, r.left, r.top, w, h, mdc, 0, 0, g_art.w, g_art.h, SRCCOPY);
        SelectObject(mdc, old);
        DeleteDC(mdc);
    } else {
        ui_round(dc, r.left, r.top, w, h, S(10), C_CARD, CLR_INVALID);
        ui_glyph(dc, GLYPH_NOTE, r.left + w / 2 - S(24), r.top + h / 2 - S(38), S(48), C_TEXT3);
        {
            RECT tr;
            tr.left = r.left;
            tr.right = r.right;
            tr.top = r.top + h / 2 + S(20);
            tr.bottom = tr.top + S(18);
            ui_text(dc, L"无封面", -1, &tr, C_TEXT3, ui_font(11, FW_NORMAL),
                    DT_CENTER | DT_TOP | DT_SINGLELINE);
        }
    }
    ui_round(dc, r.left, r.top, w, h, S(10), CLR_INVALID, C_LINE_SOFT);
}

static void draw_info(HDC dc)
{
    int x = L.info.left;
    int y = L.info.top;
    SyTrack *t;
    RECT r;
    HFONT f_title = ui_font(19, FW_BOLD);
    HFONT f_artist = ui_font(13, FW_NORMAL);
    HFONT f_album = ui_font(11, FW_NORMAL);
    HFONT f_lab = ui_font(10, FW_NORMAL);
    HFONT f_val = ui_font(11, FW_NORMAL);

    if (g_playing >= 0 && g_playing < g_list.count)
        t = &g_list.items[g_playing];
    else if (g_sel >= 0 && g_sel < g_list.count)
        t = &g_list.items[g_sel];
    else
        t = 0;

    if (!t) {
        r.left = x; r.right = L.info.right;
        r.top = y; r.bottom = y + S(26);
        ui_text(dc, L"未在播放", -1, &r, C_TEXT2, ui_font(15, FW_BOLD),
                DT_LEFT | DT_TOP | DT_SINGLELINE);
        r.top = y + S(32); r.bottom = r.top + S(52);
        ui_text(dc, L"拖入音乐文件或文件夹开始", -1, &r, C_TEXT3, ui_font(11, FW_NORMAL),
                DT_LEFT | DT_TOP | DT_WORDBREAK);
        return;
    }

    r.left = x; r.right = L.info.right;
    r.top = y; r.bottom = y + S(46);
    ui_text(dc, t->title, -1, &r, C_TEXT, f_title, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    y += S(48);

    r.left = x; r.right = L.info.right;
    r.top = y; r.bottom = y + S(19);
    ui_text(dc, t->artist[0] ? t->artist : L"未知歌手", -1, &r, C_ACCENT, f_artist,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += S(22);

    if (t->album[0]) {
        wchar_t buf[SY_TEXT + 32];
        r.left = x; r.right = L.info.right;
        r.top = y; r.bottom = y + S(17);
        w_copy(buf, t->album, SY_TEXT + 32);
        if (t->year[0]) {
            w_cat(buf, L"  ·  ", SY_TEXT + 32);
            w_cat(buf, t->year, SY_TEXT + 32);
        }
        ui_text(dc, buf, -1, &r, C_TEXT2, f_album, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += S(21);
    }

    y += S(6);
    ui_line(dc, x, y, L.info.right, y, C_LINE_SOFT);
    y += S(12);

    {
        int limit = L.info.bottom - S(18);
        int saved = SaveDC(dc);
        wchar_t labels[4][32];
        wchar_t values[4][96];
        wchar_t num[16];
        int i;
        int gap = S(14);
        int cw = (L.info.right - x - gap) / 2;
        int rh = S(38);

        if (saved)
            IntersectClipRect(dc, L.info.left - S(2), L.info.top - S(2),
                              L.info.right + S(2), limit);

        w_copy(labels[0], L"格式", 32);
        w_copy(values[0], fmt_name(t->format), 96);
        if (t->bitrate > 0) {
            w_cat(values[0], L"  ·  ", 96);
            w_from_int(t->bitrate, num, 16);
            w_cat(values[0], num, 96);
            w_cat(values[0], L" kbps", 96);
        }

        w_copy(labels[1], L"采样率", 32);
        if (t->sample_rate > 0) {
            w_from_int(t->sample_rate / 1000, num, 16);
            w_copy(values[1], num, 96);
            w_cat(values[1], L".", 96);
            w_from_int((t->sample_rate % 1000) / 100, num, 16);
            w_cat(values[1], num, 96);
            w_cat(values[1], L" kHz", 96);
            w_cat(values[1], t->channels >= 2
              ? (g_lang ? L"  ·  Stereo" : L"  ·  立体声")
              : (g_lang ? L"  ·  Mono" : L"  ·  单声道"), 96);
        } else {
            w_copy(values[1], L"--", 96);
        }

        w_copy(labels[2], L"时长", 32);
        fmt_time(t->duration_ms, values[2], 96);

        w_copy(labels[3], L"大小", 32);
        fmt_size(t->file_size, values[3], 96);

        for (i = 0; i < 4; i++) {
            int col = i & 1;
            int rowi = i >> 1;
            int cx = x + col * (cw + gap);
            int cy = y + rowi * rh;
            RECT lr, vr;
            if (cy + S(34) > limit)
                continue;
            lr.left = cx; lr.right = cx + cw;
            lr.top = cy; lr.bottom = cy + S(14);
            ui_text(dc, labels[i], -1, &lr, C_TEXT3, f_lab, DT_LEFT | DT_TOP | DT_SINGLELINE);
            vr.left = cx; vr.right = cx + cw;
            vr.top = cy + S(15); vr.bottom = cy + S(34);
            ui_text(dc, values[i], -1, &vr, C_TEXT, f_val,
                    DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        y += rh * 2;
        if (saved)
            RestoreDC(dc, saved);
    }

    if ((t->track_no > 0 || t->genre[0]) && y + S(18) < L.info.bottom - S(22)) {
        wchar_t buf[128], num[16];
        RECT gr;
        buf[0] = 0;
        if (t->track_no > 0) {
            w_from_int(t->track_no, num, 16);
            if (g_lang) {
                w_copy(buf, L"Track ", 128);
                w_cat(buf, num, 128);
            } else {
                w_copy(buf, L"第 ", 128);
                w_cat(buf, num, 128);
                w_cat(buf, L" 首", 128);
            }
        }
        if (t->genre[0]) {
            if (buf[0])
                w_cat(buf, L"  ·  ", 128);
            w_cat(buf, t->genre, 128);
        }
        gr.left = x; gr.right = L.info.right;
        gr.top = y; gr.bottom = y + S(18);
        ui_text(dc, buf, -1, &gr, C_TEXT3, ui_font(10, FW_NORMAL),
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    {
        RECT fr;
        wchar_t base[SY_TEXT];
        path_base(t->path, base, SY_TEXT);
        fr.left = x; fr.right = L.info.right;
        fr.top = L.info.bottom - S(16);
        fr.bottom = L.info.bottom;
        ui_text(dc, base, -1, &fr, C_TEXT3, ui_font(9, FW_NORMAL),
                DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void draw_pill_button(HDC dc, const RECT *r, int hovered, int primary,
                             const wchar_t *label, int glyph)
{
    int rad = S(8);
    HFONT f = ui_font(12, FW_NORMAL);
    int tw = ui_text_w(dc, label, -1, f);
    int gw = glyph ? S(14) : 0;
    int gap = glyph ? S(8) : 0;
    int total = tw + gw + gap;
    int tx, gx;

    if (primary)
        ui_round(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, rad,
                 hovered ? RGB(0x64, 0xE8, 0xB7) : C_ACCENT, CLR_INVALID);
    else
        ui_round(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, rad,
                 hovered ? C_HOVER : CLR_INVALID, hovered ? CLR_INVALID : C_LINE);

    gx = r->left + ((r->right - r->left) - total) / 2;
    tx = gx + gw + gap;
    if (glyph)
        ui_glyph(dc, glyph, gx, r->top + (r->bottom - r->top - gw) / 2, gw,
                 primary ? RGB(0x0B, 0x18, 0x13) : C_TEXT2);
    ui_text_at(dc, label, -1, tx, r->top + (r->bottom - r->top - S(16)) / 2,
               primary ? RGB(0x0B, 0x18, 0x13) : C_TEXT2, f);
}

static void draw_search(HDC dc)
{
    RECT r = L.search;
    int rad = S(8);
    ui_round(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, rad, C_CARD,
             g_search_on ? C_ACCENT_D : C_LINE);
    ui_glyph(dc, GLYPH_SEARCH, r.left + S(12), r.top + S(10), S(14), C_TEXT3);
    {
        RECT tr;
        tr.left = r.left + S(34);
        tr.right = r.right - S(12);
        tr.top = r.top + S(9);
        tr.bottom = r.top + S(28);
        if (g_search[0] || g_search_on) {
            wchar_t wq[128];
            w_from_ansi(g_search, wq, 128);
            ui_text(dc, wq, -1, &tr, C_TEXT, ui_font(12, FW_NORMAL),
                    DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (g_search_on && g_caret) {
                int tw = ui_text_w(dc, wq, -1, ui_font(12, FW_NORMAL));
                ui_fill(dc, tr.left + tw + S(1), tr.top + S(2), 1, S(15), C_ACCENT);
            }
        } else {
            ui_text(dc, L"搜索歌曲、歌手、专辑", -1, &tr, C_TEXT3, ui_font(12, FW_NORMAL),
                    DT_LEFT | DT_TOP | DT_SINGLELINE);
        }
    }
}

static void draw_playlist(HDC dc)
{
    HFONT f_title = ui_font(13, FW_NORMAL);
    HFONT f_sub = ui_font(11, FW_NORMAL);
    HFONT f_col = ui_font(10, FW_NORMAL);
    int col_title = L.list.left + S(46);
    int col_right = L.list.right;
    int col_album = col_title + (col_right - col_title) * 58 / 100;
    int col_time_w = g_lang ? S(76) : S(52);   /* "Length" needs more room than 时长 */
    int i;

    draw_search(dc);

    {
        RECT tr;
        wchar_t buf[96], num[16];
        long long total_ms = 0;
        int k;
        for (k = 0; k < g_view_n; k++)
            total_ms += g_list.items[g_view[k]].duration_ms;
        w_from_int(g_view_n, num, 16);
        if (g_lang) {
            w_copy(buf, num, 96);
            w_cat(buf, L" tracks  ·  ", 96);
        } else {
            w_copy(buf, L"共 ", 96);
            w_cat(buf, num, 96);
            w_cat(buf, L" 首  ·  ", 96);
        }
        fmt_time((int)(total_ms / 1000) * 1000, num, 16);
        w_cat(buf, num, 96);
        {
            int mc = mark_count();
            if (mc > 0) {
                if (g_lang) {
                    w_cat(buf, L"  ·  ", 96);
                    w_from_int(mc, num, 16);
                    w_cat(buf, num, 96);
                    w_cat(buf, L" selected", 96);
                } else {
                    w_cat(buf, L"  ·  已选 ", 96);
                    w_from_int(mc, num, 16);
                    w_cat(buf, num, 96);
                }
            }
        }
        tr.left = L.list.left + S(292);
        tr.right = L.list.right;
        tr.top = L.search.top + S(9);
        tr.bottom = tr.top + S(18);
        ui_text(dc, buf, -1, &tr, C_TEXT3, ui_font(11, FW_NORMAL),
                DT_RIGHT | DT_TOP | DT_SINGLELINE);
    }

    {
        RECT tr;
        int hy = L.rows_top - S(22);
        tr.top = hy;
        tr.bottom = hy + S(16);
        tr.left = col_title;
        tr.right = col_album - S(10);
        ui_text(dc, g_sort == 1 ? L"标题 ▼" : L"标题", -1, &tr,
                g_sort == 1 ? C_ACCENT : C_TEXT3, f_col, DT_LEFT | DT_TOP | DT_SINGLELINE);
        tr.left = col_album;
        tr.right = col_right - col_time_w - S(4);
        ui_text(dc, g_sort == 3 ? L"专辑 ▼" : L"专辑", -1, &tr,
                g_sort == 3 ? C_ACCENT : C_TEXT3, f_col, DT_LEFT | DT_TOP | DT_SINGLELINE);
        tr.left = col_right - col_time_w;
        tr.right = col_right;
        ui_text(dc, g_sort == 4 ? L"时长 ▼" : L"时长", -1, &tr,
                g_sort == 4 ? C_ACCENT : C_TEXT3, f_col, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        ui_line(dc, L.list.left, L.rows_top - S(6), L.list.right, L.rows_top - S(6), C_LINE_SOFT);
    }

    if (g_list.count == 0) {
        RECT r;
        int cx = (L.list.left + L.list.right) / 2;
        int cy = L.rows_top + L.view_h / 2;
        ui_glyph(dc, GLYPH_MUSIC, cx - S(24), cy - S(78), S(48), C_TEXT3);
        r.left = L.list.left; r.right = L.list.right;
        r.top = cy - S(18); r.bottom = cy + S(6);
        ui_text(dc, L"还没有音乐", -1, &r, C_TEXT, ui_font(15, FW_BOLD),
                DT_CENTER | DT_TOP | DT_SINGLELINE);
        r.top = cy + S(10); r.bottom = cy + S(32);
        ui_text(dc, L"添加文件、文件夹，或直接拖入窗口", -1, &r, C_TEXT3,
                ui_font(11, FW_NORMAL), DT_CENTER | DT_TOP | DT_SINGLELINE);
        draw_pill_button(dc, &L.empty_btn, g_hover == HOV_ADDF, 1, L"添加文件", GLYPH_PLUS);
        return;
    }

    {
        int saved = SaveDC(dc);
        IntersectClipRect(dc, L.list.left - S(16), L.rows_top - S(2),
                          L.list.right + S(16), L.list.bottom);
    {
    int maxs = g_view_n * L.row_h - L.view_h;
    if (maxs < 0)
        maxs = 0;
    if (g_scroll > maxs)
        g_scroll = maxs;
    if (g_scroll < 0)
        g_scroll = 0;
    int first = L.row_h > 0 ? (g_scroll / L.row_h) : 0;
    int last = first + (L.row_h > 0 ? (L.view_h / L.row_h) : 0) + 2;
    if (first < 0)
        first = 0;
    if (last > g_view_n)
        last = g_view_n;
    for (i = first; i < last; i++) {
        int y = L.rows_top + i * L.row_h - g_scroll;
        int idx;
        if (g_drag_row >= 0 && g_from_pos >= 0) {
            int sh = row_shift(i);
            if (sh)
                y += sh * L.row_h * ease_shift() / 256;
        }
        idx = g_view[i];
        SyTrack *t = &g_list.items[idx];
        RECT rr, tr;
        int playing = (idx == g_playing);
        int selected = (idx == g_sel);
        int marked = (g_mark && idx < g_mark_cap && g_mark[idx]);
        int hovered = (idx == g_hover);

        if (y + L.row_h < L.rows_top - 2 || y > L.list.bottom + L.row_h)
            continue;
        if (idx == g_drag_row)
            continue;                /* drawn separately, under the cursor */
        rr.left = L.list.left;
        rr.right = L.list.right;
        rr.top = y;
        rr.bottom = y + L.row_h;

        ui_round(dc, rr.left - S(8), rr.top + S(2), (rr.right - rr.left) + S(16),
                 L.row_h - S(4), S(8),
                 (playing || selected || marked) ? C_SELECT : (hovered ? C_HOVER : C_BG),
                 CLR_INVALID);
        if (playing)
            ui_fill(dc, rr.left - S(8), rr.top + S(12), S(2), L.row_h - S(24), C_ACCENT);

        tr.left = rr.left;
        tr.right = rr.left + S(36);
        tr.top = rr.top + S(10);
        tr.bottom = rr.bottom;
        if (playing && !g_paused) {
            ui_eq(dc, rr.left + S(9), rr.top + S(19), S(15), C_ACCENT, g_anim);
        } else if (playing) {
            ui_glyph(dc, GLYPH_PAUSE, rr.left + S(9), rr.top + S(20), S(13), C_ACCENT);
        } else {
            wchar_t num[16];
            w_from_int(i + 1, num, 16);
            ui_text(dc, num, -1, &tr, marked ? C_ACCENT : C_TEXT3, ui_font(11, FW_NORMAL),
                    DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }

        tr.left = col_title;
        tr.right = col_album - S(12);
        tr.top = rr.top + S(9);
        tr.bottom = tr.top + S(18);
        ui_text(dc, t->title, -1, &tr, playing ? C_ACCENT : C_TEXT, f_title,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        tr.top = rr.top + S(28);
        tr.bottom = tr.top + S(16);
        ui_text(dc, t->artist[0] ? t->artist : L"未知歌手", -1, &tr, C_TEXT2, f_sub,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        tr.left = col_album;
        tr.right = col_right - S(58);
        tr.top = rr.top + S(18);
        tr.bottom = tr.top + S(18);
        ui_text(dc, t->album[0] ? t->album : L"--", -1, &tr, C_TEXT2, f_sub,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        {
            wchar_t tm[32];
            fmt_time(t->duration_ms, tm, 32);
            tr.left = col_right - col_time_w;
            tr.right = col_right;
            tr.top = rr.top + S(18);
            tr.bottom = tr.top + S(18);
            ui_text(dc, tm, -1, &tr, C_TEXT3, ui_font(11, FW_NORMAL),
                    DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
    }

    {
        int content = g_view_n * L.row_h;
        if (content > L.view_h) {
            int track_h = L.view_h;
            int thumb = track_h * L.view_h / content;
            int maxs = content - L.view_h;
            int ty;
            if (thumb < S(24))
                thumb = S(24);
            ty = L.rows_top + (track_h - thumb) * g_scroll / (maxs ? maxs : 1);
            ui_round(dc, L.list.right + S(2), L.rows_top, S(4), track_h, S(2), C_LINE_SOFT, CLR_INVALID);
            ui_round(dc, L.list.right + S(2), ty, S(4), thumb, S(2), C_TEXT3, CLR_INVALID);
        }
    }
    }
    if (g_drag_row >= 0 && g_drop_pos >= 0) {
        int ly = L.rows_top + g_drop_pos * L.row_h - g_scroll;
        ui_fill(dc, L.list.left - S(8), ly - S(1), (L.list.right - L.list.left) + S(16),
                S(2), C_ACCENT);
    }
    RestoreDC(dc, saved);
    }

    if (g_drag_row >= 0 && g_drop_pos >= 0) {
        /* the lifted row, following the pointer */
        RECT dr;
        int py = g_drag_y - L.row_h / 2;
        SyTrack *dt;
        if (py < L.rows_top) py = L.rows_top;
        if (py + L.row_h > L.list.bottom) py = L.list.bottom - L.row_h;
        if (g_drag_row < 0 || g_drag_row >= g_list.count)
            return;
        dt = &g_list.items[g_drag_row];
        dr.left = L.list.left;
        dr.right = L.list.right;
        dr.top = py;
        dr.bottom = py + L.row_h;
        {
            int sv = SaveDC(dc);
            IntersectClipRect(dc, L.list.left - S(16), L.rows_top,
                              L.list.right + S(16), L.list.bottom);
            ui_round(dc, dr.left - S(8), dr.top + S(2),
                     (dr.right - dr.left) + S(16), L.row_h - S(4), S(8), C_HOVER, C_ACCENT);
            {
                RECT tr;
                tr.left = dr.left + S(8);
                tr.right = dr.right - S(8);
                tr.top = dr.top + S(12);
                tr.bottom = tr.top + S(20);
                ui_text(dc, dt->title[0] ? dt->title : L"", -1, &tr, C_TEXT,
                        ui_font(13, FW_NORMAL), DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
                tr.top = dr.top + S(32);
                tr.bottom = tr.top + S(18);
                ui_text(dc, dt->artist[0] ? dt->artist : L"未知歌手", -1, &tr, C_TEXT2,
                        ui_font(11, FW_NORMAL), DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            RestoreDC(dc, sv);
        }
    }
}

static void draw_mode(HDC dc)
{
    RECT r = L.mode;
    RECT tr;
    int gw = S(18);
    int hov = (g_hover == HOV_MODE);

    ui_round(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, S(8),
             hov ? C_CARD : CLR_INVALID, hov ? CLR_INVALID : C_LINE_SOFT);
    ui_glyph(dc, mode_glyph(g_mode), r.left + S(12),
             r.top + (r.bottom - r.top - gw) / 2, gw, C_ACCENT);
    tr.left = r.left + S(12) + gw + S(8);
    tr.right = r.right - S(8);
    tr.top = r.top + (r.bottom - r.top - S(16)) / 2;
    tr.bottom = tr.top + S(16);
    ui_text(dc, mode_name(g_mode), -1, &tr, hov ? C_TEXT : C_TEXT2,
            ui_font(11, FW_NORMAL), DT_LEFT | DT_TOP | DT_SINGLELINE);
}

static void draw_bottom(HDC dc, int W, int H)
{
    int bar_top = H - S(BAR_H);
    int pad = S(28);
    HFONT f_time = ui_font(11, FW_NORMAL);
    int pct = 0;
    int total = g_dur;

    if (!g_active && g_sel >= 0 && g_sel < g_list.count)
        total = g_list.items[g_sel].duration_ms;

    ui_fill(dc, 0, bar_top, W, H - bar_top, C_BG);
    ui_line(dc, 0, bar_top, W, bar_top, C_LINE_SOFT);

    if (g_dur > 0)
        pct = (int)((long long)g_pos * 10000 / g_dur);
    if (pct < 0)
        pct = 0;
    if (pct > 10000)
        pct = 10000;

    {
        int l = L.seek.left, r = L.seek.right;
        int y = (L.seek.top + L.seek.bottom) / 2;
        int fill = l + (r - l) * pct / 10000;
        ui_round(dc, l, y - S(2), r - l, S(4), S(2), C_LINE, CLR_INVALID);
        if (fill > l)
            ui_round(dc, l, y - S(2), fill - l, S(4), S(2), C_ACCENT, CLR_INVALID);
        if (g_active) {
            HBRUSH b = CreateSolidBrush(C_TEXT);
            HBRUSH ob = (HBRUSH)SelectObject(dc, b);
            HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, fill - S(5), y - S(5), fill + S(5), y + S(5));
            SelectObject(dc, op);
            SelectObject(dc, ob);
            DeleteObject(b);
        }
        {
            wchar_t buf[32];
            RECT tr;
            fmt_time(g_pos, buf, 32);
            tr.left = pad; tr.right = pad + S(44);
            tr.top = L.seek.top + S(2); tr.bottom = tr.top + S(16);
            ui_text(dc, buf, -1, &tr, C_TEXT2, f_time, DT_LEFT | DT_TOP | DT_SINGLELINE);
            fmt_time(total, buf, 32);
            tr.left = W - pad - S(44); tr.right = W - pad;
            ui_text(dc, buf, -1, &tr, C_TEXT3, f_time, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
    }

    ui_glyph(dc, GLYPH_PREV, L.prev.left + S(4), L.prev.top + S(5), S(24),
             g_hover == HOV_GLYPH(GLYPH_PREV) ? C_TEXT : C_TEXT2);
    ui_glyph(dc, GLYPH_NEXT, L.next.left + S(5), L.next.top + S(5), S(24),
             g_hover == HOV_GLYPH(GLYPH_NEXT) ? C_TEXT : C_TEXT2);

    {
        int cx = (L.play.left + L.play.right) / 2;
        int cy = (L.play.top + L.play.bottom) / 2;
        HBRUSH b = CreateSolidBrush(g_hover == HOV_GLYPH(GLYPH_PLAY) ? RGB(0x6A, 0xEB, 0xBB) : C_ACCENT);
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, L.play.left, L.play.top, L.play.right, L.play.bottom);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(b);
        if (g_active && !g_paused)
            ui_glyph(dc, GLYPH_PAUSE, cx - S(8), cy - S(9), S(18), RGB(0x0B, 0x18, 0x13));
        else
            ui_glyph(dc, GLYPH_PLAY, cx - S(7), cy - S(9), S(18), RGB(0x0B, 0x18, 0x13));
    }

    draw_mode(dc);

    {
        int l = L.vol.left, r = L.vol.right;
        int y = (L.vol.top + L.vol.bottom) / 2;
        int fill = l + (r - l) * g_vol / 100;
        ui_glyph(dc, g_vol == 0 ? GLYPH_VOL_MUTE : GLYPH_VOL, l - S(26), y - S(9), S(18), C_TEXT2);
        ui_round(dc, l, y - S(2), r - l, S(4), S(2), C_LINE, CLR_INVALID);
        if (fill > l)
            ui_round(dc, l, y - S(2), fill - l, S(4), S(2), C_ACCENT, CLR_INVALID);
        {
            HBRUSH b = CreateSolidBrush(C_TEXT2);
            HBRUSH ob = (HBRUSH)SelectObject(dc, b);
            HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, fill - S(4), y - S(4), fill + S(4), y + S(4));
            SelectObject(dc, op);
            SelectObject(dc, ob);
            DeleteObject(b);
        }
    }
}

static void draw_toast(HDC dc, int W, int H)
{
    HFONT f;
    int tw, w, x, y;
    RECT tr;
    if (!g_toast[0])
        return;
    if (GetTickCount() - g_toast_at > 4000) {
        g_toast[0] = 0;
        return;
    }
    f = ui_font(11, FW_NORMAL);
    tw = ui_text_w(dc, g_toast, -1, f);
    w = tw + S(30);
    x = (W - w) / 2;
    y = H - S(BAR_H) - S(42);
    ui_round(dc, x, y, w, S(28), S(14), C_CARD, C_LINE);
    tr.left = x; tr.right = x + w;
    tr.top = y + S(6); tr.bottom = y + S(24);
    ui_text(dc, g_toast, -1, &tr, C_TEXT, f, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

static int g_settings;
static int g_assoc_on;
static int g_assoc_err;
static int g_assoc_note;
static int g_low_integrity;
static int g_min_to_tray = 1;
static RECT g_tray_row;
static RECT g_relaunch_btn;
static int  g_mini;
static int  g_mini_on_min = 1;
static int  g_close_to_tray;
static int  g_mini_top = 1;
static int  g_fade_on = 1;
static RECT m_cover, m_prev, m_play, m_next, m_seek;
static RECT g_full_rect;
static RECT g_mini_row;
static RECT g_close_row;
static RECT g_top_row;
static RECT g_fade_row;
static RECT g_gapless_row;
static int  g_mini_drag_ms;
static int  g_mini_dragging;
static DWORD g_sleep_at;
static int g_win_x = -1, g_win_y, g_win_w, g_win_h;
static int g_geom_pending;
static int g_seek_preview = -1;
static int g_fast_bar;
static DWORD g_preview_tick;
static int g_layout_w = -1, g_layout_h = -1;
static int  g_device = -1;
#define RECENT_N 32
static char g_recent_path[RECENT_N][SY_PATH];
static int  g_recent_ms[RECENT_N];
static int  g_recent_seq[RECENT_N];
static int  g_recent_tick;
static int  recent_get(const char *path);
static void recent_set(const char *path, int ms);
static RECT m_restore;
static int  g_mini_w, g_mini_h;
static RECT g_assoc_row;

static void draw_gear(HDC dc, const RECT *r, int hovered)
{
    int cx = (r->left + r->right) / 2;
    int cy = (r->top + r->bottom) / 2;
    int rad = S(9);
    int i;
    COLORREF c = hovered ? C_TEXT : C_TEXT2;

    if (hovered)
        ui_round(dc, r->left, r->top, r->right - r->left, r->bottom - r->top,
                 (r->bottom - r->top) / 2, C_PANEL, CLR_INVALID);
    for (i = 0; i < 8; i++) {
        static const int dx[8] = { 0, 7, 10, 7, 0, -7, -10, -7 };
        static const int dy[8] = { -10, -7, 0, 7, 10, 7, 0, -7 };
        int x1 = cx + dx[i] * rad / 10, y1 = cy + dy[i] * rad / 10;
        int x2 = cx + dx[i] * (rad + S(3)) / 10, y2 = cy + dy[i] * (rad + S(3)) / 10;
        ui_line(dc, x1, y1, x2, y2, c);
    }
    {
        HPEN p = CreatePen(PS_SOLID, 2, c);
        HPEN op = (HPEN)SelectObject(dc, p);
        HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx - rad, cy - rad, cx + rad, cy + rad);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(p);
    }
    ui_round(dc, cx - S(2), cy - S(2), S(4), S(4), S(2), c, CLR_INVALID);
}

static void draw_check(HDC dc, int x, int y, int on)
{
    int sz = S(16);
    ui_round(dc, x, y, sz, sz, S(4), on ? C_ACCENT : C_PANEL, on ? C_ACCENT : C_LINE);
    if (on) {
        HPEN p = CreatePen(PS_SOLID, 2, RGB(0x0B, 0x18, 0x13));
        HPEN op = (HPEN)SelectObject(dc, p);
        MoveToEx(dc, x + S(4), y + S(8), NULL);
        LineTo(dc, x + S(7), y + S(11));
        LineTo(dc, x + S(12), y + S(5));
        SelectObject(dc, op);
        DeleteObject(p);
    }
}

static void draw_settings(HDC dc, int W, int H)
{
    int pad = S(28);
    int cw = S(560);
    int ch = S(556);
    int x, y;

    if (cw > W - pad * 2)
        cw = W - pad * 2;
    x = (W - cw) / 2;
    y = S(TOP_H) + S(28);

    ui_fill(dc, 0, 0, W, H, C_BG);
    {
        HBRUSH b = CreateSolidBrush(RGB(0x3F, 0xC9, 0x96));
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, pad, S(17), pad + S(30), S(47), S(9), S(9));
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(b);
        ui_glyph(dc, GLYPH_NOTE, pad + S(5), S(20), S(20), RGB(0x0B, 0x18, 0x13));
    }
    ui_text_at(dc, L"设置", -1, pad + S(42), S(16), C_TEXT, ui_font(16, FW_BOLD));
    draw_pill_button(dc, &L.back, g_hover == HOV_FOLDER, 0, L"返回", 0);
    ui_line(dc, 0, S(TOP_H), W, S(TOP_H), C_LINE_SOFT);

    ui_round(dc, x, y, cw, ch, S(14), C_PANEL, C_LINE);

    ui_text_at(dc, L"文件关联", -1, x + S(24), y + S(20), C_TEXT, ui_font(13, FW_BOLD));
    ui_text_at(dc, L"只有在这里勾选才会写入注册表，启动软件不会自动修改。", -1,
               x + S(24), y + S(44), C_TEXT3, ui_font(9, FW_NORMAL));

    g_assoc_row.left = x + S(20);
    g_assoc_row.top = y + S(68);
    g_assoc_row.right = x + cw - S(20);
    g_assoc_row.bottom = g_assoc_row.top + S(30);
    if (g_hover == HOV_ADDF)
        ui_round(dc, g_assoc_row.left, g_assoc_row.top, g_assoc_row.right - g_assoc_row.left,
                 S(30), S(8), C_LINE_SOFT, CLR_INVALID);
    draw_check(dc, x + S(24), g_assoc_row.top + S(7), g_assoc_on);
    ui_text_at(dc, L"将拾音设为音频文件的默认打开程序", -1, x + S(52),
               g_assoc_row.top + S(5), C_TEXT, ui_font(12, FW_NORMAL));

    ui_line(dc, x + S(24), g_assoc_row.bottom + S(10), x + cw - S(24),
            g_assoc_row.bottom + S(10), C_LINE_SOFT);

    g_tray_row.left = x + S(20);
    g_tray_row.top = g_assoc_row.bottom + S(20);
    g_tray_row.right = x + cw - S(20);
    g_tray_row.bottom = g_tray_row.top + S(30);
    if (g_hover == HOV_GEAR)
        ui_round(dc, g_tray_row.left, g_tray_row.top, g_tray_row.right - g_tray_row.left,
                 S(30), S(8), C_LINE_SOFT, CLR_INVALID);
    draw_check(dc, x + S(24), g_tray_row.top + S(7), g_min_to_tray);
    ui_text_at(dc, L"最小化时隐藏到托盘", -1, x + S(52), g_tray_row.top + S(5),
               C_TEXT, ui_font(12, FW_NORMAL));


    {
        RECT tr;
        tr.left = x + S(24);
        tr.right = x + cw - S(24);
    g_mini_row.left = x + S(20);
    g_mini_row.top = g_tray_row.top + S(42);
    g_mini_row.right = x + cw - S(20);
    g_mini_row.bottom = g_mini_row.top + S(30);
    draw_check(dc, x + S(24), g_mini_row.top + S(7), g_mini_on_min);
    ui_text_at(dc, L"\u6700\u5c0f\u5316\u65f6\u663e\u793a\u8ff7\u4f60\u6a21\u5f0f", -1, x + S(52),
               g_mini_row.top + S(5), C_TEXT, ui_font(12, FW_NORMAL));

    g_close_row.left = x + S(20);
    g_close_row.top = g_mini_row.top + S(42);
    g_close_row.right = x + cw - S(20);
    g_close_row.bottom = g_close_row.top + S(30);
    draw_check(dc, x + S(24), g_close_row.top + S(7), g_close_to_tray);
    ui_text_at(dc, L"\u5173\u95ed\u7a97\u53e3\u65f6\u6536\u8fdb\u6258\u76d8", -1, x + S(52),
               g_close_row.top + S(5), C_TEXT, ui_font(12, FW_NORMAL));

    g_top_row.left = x + S(20);
    g_top_row.top = g_close_row.top + S(42);
    g_top_row.right = x + cw - S(20);
    g_top_row.bottom = g_top_row.top + S(30);
    draw_check(dc, x + S(24), g_top_row.top + S(7), g_mini_top);
    ui_text_at(dc, L"\u8ff7\u4f60\u7a97\u53e3\u603b\u5728\u6700\u524d", -1, x + S(52),
               g_top_row.top + S(5), C_TEXT, ui_font(12, FW_NORMAL));

    g_fade_row.left = x + S(20);
    g_fade_row.top = g_top_row.top + S(42);
    g_fade_row.right = x + cw - S(20);
    g_fade_row.bottom = g_fade_row.top + S(30);
    draw_check(dc, x + S(24), g_fade_row.top + S(7), g_fade_on);
    ui_text_at(dc, L"\u64ad\u653e\u65f6\u6de1\u5165\u6de1\u51fa", -1, x + S(52),
               g_fade_row.top + S(5), C_TEXT, ui_font(12, FW_NORMAL));

    g_gapless_row.left = x + S(20);
    g_gapless_row.top = g_fade_row.top + S(42);
    g_gapless_row.right = x + cw - S(20);
    g_gapless_row.bottom = g_gapless_row.top + S(30);
    draw_check(dc, x + S(24), g_gapless_row.top + S(7), g_gapless_on);
    ui_text_at(dc, L"\u65e0\u7f1d\u8854\u63a5\uff08\u66f2\u95f4\u4e0d\u4e2d\u65ad\uff09", -1, x + S(52),
               g_gapless_row.top + S(5), C_TEXT, ui_font(12, FW_NORMAL));

    g_theme_row.left = x + S(20);
    g_theme_row.top = g_gapless_row.top + S(42);
    g_theme_row.right = x + cw - S(20);
    g_theme_row.bottom = g_theme_row.top + S(30);
    {
        const wchar_t *label = g_theme == 0 ? L"\u5916\u89c2\uff1a\u6df1\u8272"
                              : g_theme == 1 ? L"\u5916\u89c2\uff1a\u6d45\u8272"
                                             : L"\u5916\u89c2\uff1a\u8ddf\u968f\u65f6\u95f4";
        ui_text_at(dc, label, -1, x + S(24), g_theme_row.top + S(5), C_TEXT, ui_font(12, FW_NORMAL));
        ui_text_at(dc, L"\u70b9\u4e00\u4e0b\u5207\u6362\u6df1\u8272 / \u6d45\u8272 / \u8ddf\u968f\u65f6\u95f4", -1,
                   x + S(24), g_theme_row.top + S(23), C_TEXT3, ui_font(9, FW_NORMAL));
    }

    g_lang_row.left = x + S(20);
    g_lang_row.top = g_theme_row.top + S(42);
    g_lang_row.right = x + cw - S(20);
    g_lang_row.bottom = g_lang_row.top + S(30);
    ui_text_at(dc, g_lang ? L"\u8bed\u8a00\uff1aEnglish" : L"\u8bed\u8a00\uff1a\u4e2d\u6587", -1,
               x + S(24), g_lang_row.top + S(5), C_TEXT, ui_font(12, FW_NORMAL));
    ui_text_at(dc, L"\u70b9\u4e00\u4e0b\u5207\u6362\u754c\u9762\u8bed\u8a00", -1,
               x + S(24), g_lang_row.top + S(23), C_TEXT3, ui_font(9, FW_NORMAL));

        tr.top = g_lang_row.bottom + S(8);
        tr.bottom = tr.top + S(48);
        ui_text(dc,
                L"支持 MP3 / FLAC / WAV / AIFF / AU。Windows 95/98/XP 会直接写入关联；"
                L"Windows 10/11 不允许程序改默认，会打开系统设置让你选择拾音。"
                L"取消勾选只移除本程序写入的项。",
                -1, &tr, C_TEXT2, ui_font(10, FW_NORMAL), DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    {
        RECT sr;
        wchar_t buf[160], num[16];
        sr.left = x + S(24);
        sr.right = x + cw - S(24);
        sr.top = y + ch - S(46);
        sr.bottom = sr.top + S(20);
        if (g_assoc_err == 5 && assoc_integrity_low()) {
            ui_text(dc, L"无法写入注册表：当前进程权限受限（低完整性）", -1, &sr,
                    RGB(0xE0, 0x6C, 0x6C), ui_font(10, FW_NORMAL),
                    DT_LEFT | DT_TOP | DT_SINGLELINE);
            {
                RECT pr = sr;
                pr.top += S(18);
                pr.bottom += S(18);
                ui_text(dc, L"低完整性进程没有注册表写权限，点右侧按钮可重新以正常权限启动", -1,
                        &pr, C_TEXT3, ui_font(9, FW_NORMAL), DT_LEFT | DT_TOP | DT_SINGLELINE);
            }
        } else if (g_assoc_err) {
            w_copy(buf, L"写入注册表失败（错误 ", 160);
            w_from_int(g_assoc_err, num, 16);
            w_cat(buf, num, 160);
            w_cat(buf, L"）", 160);
            ui_text(dc, buf, -1, &sr, RGB(0xE0, 0x6C, 0x6C), ui_font(10, FW_NORMAL),
                    DT_LEFT | DT_TOP | DT_SINGLELINE);
        } else if (g_assoc_note) {
            ui_text(dc, L"已登记为候选程序，请在系统设置中选择「拾音」", -1, &sr, C_ACCENT,
                    ui_font(10, FW_NORMAL), DT_LEFT | DT_TOP | DT_SINGLELINE);
        } else if (g_assoc_on) {
            ui_text(dc, L"当前状态：已设为默认打开方式", -1, &sr, C_ACCENT,
                    ui_font(10, FW_NORMAL), DT_LEFT | DT_TOP | DT_SINGLELINE);
        } else {
            ui_text(dc, L"当前状态：未设置（系统默认程序不变）", -1, &sr, C_TEXT3,
                    ui_font(10, FW_NORMAL), DT_LEFT | DT_TOP | DT_SINGLELINE);
        }
    }

    if (g_low_integrity) {
        g_relaunch_btn.right = x + cw - S(20);
        g_relaunch_btn.left = g_relaunch_btn.right - S(150);
        g_relaunch_btn.top = y + ch - S(50);
        g_relaunch_btn.bottom = g_relaunch_btn.top + S(30);
        draw_pill_button(dc, &g_relaunch_btn, g_hover == HOV_GEAR, 1,
                         L"以正常权限重启", 0);
    }

    draw_toast(dc, W, H);
}

static void draw_mini(HDC dc, int W, int H)
{
    int cover = H - S(28);
    int i, cur;
    RECT r;
    wchar_t line[SY_TEXT];

    ui_fill(dc, 0, 0, W, H, C_BG);
    m_cover.left = S(14);
    m_cover.top = S(14);
    m_cover.right = m_cover.left + cover;
    m_cover.bottom = m_cover.top + cover;
    if (g_art.bmp) {
        HDC mdc = CreateCompatibleDC(dc);
        HBITMAP old = (HBITMAP)SelectObject(mdc, g_art.bmp);
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, m_cover.left, m_cover.top, cover, cover, mdc, 0, 0,
                   g_art.w, g_art.h, SRCCOPY);
        SelectObject(mdc, old);
        DeleteDC(mdc);
    } else {
        ui_round(dc, m_cover.left, m_cover.top, cover, cover, S(8), C_PANEL, C_LINE);
        ui_glyph(dc, GLYPH_NOTE, m_cover.left + cover / 3, m_cover.top + cover / 3,
                 cover / 3, C_TEXT3);
    }

    if (g_sel >= 0 && g_sel < g_list.count) {
        SyTrack *t = &g_list.items[g_sel];
        w_copy(line, t->title[0] ? t->title : L"", SY_TEXT);
    } else {
        w_copy(line, L"未选择曲目", SY_TEXT);
    }
    r.left = m_cover.right + S(12);
    r.right = W - S(96);
    r.top = S(16);
    r.bottom = r.top + S(22);
    ui_text(dc, line, -1, &r, C_TEXT, ui_font(13, FW_BOLD),
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    line[0] = 0;
    if (g_sel >= 0 && g_sel < g_list.count) {
        SyTrack *t = &g_list.items[g_sel];
        w_copy(line, t->artist[0] ? t->artist : L"未知歌手", SY_TEXT);
        if (t->album[0]) {
            w_cat(line, L"  ·  ", SY_TEXT);
            w_cat(line, t->album, SY_TEXT);
        }
    }
    r.top = S(38);
    r.bottom = r.top + S(18);
    ui_text(dc, line, -1, &r, C_TEXT3, ui_font(10, FW_NORMAL),
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (g_sel >= 0 && g_sel < g_list.count)
        lrc_load(g_list.items[g_sel].path);
    cur = lrc_index_at(g_pos);
    if (lrc_count() > 0 && cur >= 0) {
        r.top = S(60);
        r.bottom = r.top + S(20);
        ui_text(dc, lrc_text(cur), -1, &r, C_ACCENT, ui_font(11, FW_NORMAL),
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    m_seek.left = m_cover.right + S(12);
    m_seek.right = W - S(100);
    m_seek.top = H - S(24);
    m_seek.bottom = m_seek.top + S(6);
    ui_round(dc, m_seek.left, m_seek.top, m_seek.right - m_seek.left, S(4), S(2),
             C_LINE, CLR_INVALID);
    {
        int shown = g_mini_dragging ? g_mini_drag_ms : g_pos;
        int pct = g_dur > 0 ? shown * 100 / g_dur : 0;
        if (pct > 100) pct = 100;
        if (pct > 0)
            ui_round(dc, m_seek.left, m_seek.top,
                     (m_seek.right - m_seek.left) * pct / 100, S(4), S(2), C_ACCENT, CLR_INVALID);
        if (g_mini_dragging) {
            int kx = m_seek.left + (m_seek.right - m_seek.left) * pct / 100;
            ui_round(dc, kx - S(5), m_seek.top - S(3), S(10), S(10), S(5), C_ACCENT, C_BG);
            {
                wchar_t tb[16];
                RECT tr;
                fmt_time(shown, tb, 16);
                tr.left = kx - S(40);
                tr.right = kx + S(40);
                tr.top = m_seek.top - S(30);
                tr.bottom = tr.top + S(18);
                ui_text(dc, tb, -1, &tr, C_TEXT, ui_font(10, FW_BOLD),
                        DT_CENTER | DT_TOP | DT_SINGLELINE);
            }
        }
    }

    m_restore.right = W - S(12);
    m_restore.left = m_restore.right - S(40);
    m_restore.top = S(12);
    m_restore.bottom = m_restore.top + S(18);
    ui_round(dc, m_restore.left, m_restore.top, S(40), S(18), S(9), C_PANEL, C_LINE);
    {
        RECT rr = m_restore;
        ui_text(dc, L"还原", -1, &rr, C_TEXT2, ui_font(9, FW_NORMAL),
                DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    m_prev.right = W - S(14);
    m_prev.left = m_prev.right - S(26);
    m_prev.top = H - S(34);
    m_prev.bottom = m_prev.top + S(22);
    m_play.right = m_prev.left - S(6);
    m_play.left = m_play.right - S(26);
    m_play.top = m_prev.top;
    m_play.bottom = m_prev.bottom;
    m_next.right = m_play.left - S(6);
    m_next.left = m_next.right - S(26);
    m_next.top = m_prev.top;
    m_next.bottom = m_prev.bottom;
    ui_glyph(dc, GLYPH_PREV, m_prev.left + S(4), m_prev.top + S(4), S(18), C_TEXT2);
    ui_glyph(dc, (g_active && !g_paused) ? GLYPH_PAUSE : GLYPH_PLAY,
             m_play.left + S(4), m_play.top + S(4), S(18), C_TEXT);
    ui_glyph(dc, GLYPH_NEXT, m_next.left + S(4), m_next.top + S(4), S(18), C_TEXT2);
}

static void draw_lyrics(HDC dc, int W, int H)
{
    int saved, area_top, area_bottom, cy, i, cur;
    RECT r;

    if (g_sel >= 0 && g_sel < g_list.count)
        lrc_load(g_list.items[g_sel].path);   /* cached by path */

    ui_line(dc, 0, S(TOP_H), W, S(TOP_H), C_LINE_SOFT);
    area_top = L.rows_top - S(10);
    area_bottom = L.list.bottom - S(6);

    if (lrc_count() == 0) {
        int mcx = (L.list.left + L.list.right) / 2;
        int mcy = (area_top + area_bottom) / 2;
        ui_glyph(dc, GLYPH_MUSIC, mcx - S(20), mcy - S(64), S(40), C_TEXT3);
        r.left = L.list.left;
        r.right = L.list.right;
        r.top = mcy - S(8);
        r.bottom = r.top + S(24);
        ui_text(dc, L"没有歌词", -1, &r, C_TEXT, ui_font(14, FW_BOLD),
                DT_CENTER | DT_TOP | DT_SINGLELINE);
        r.top = mcy + S(18);
        r.bottom = r.top + S(20);
        ui_text(dc, L"把同名 .lrc 文件放在歌曲旁边即可显示", -1, &r, C_TEXT3,
                ui_font(10, FW_NORMAL), DT_CENTER | DT_TOP | DT_SINGLELINE);
        return;
    }

    cur = lrc_index_at(g_pos);
    if (cur < 0)
        cur = 0;
    cy = (area_top + area_bottom) / 2;
    saved = SaveDC(dc);
    IntersectClipRect(dc, L.list.left, area_top, L.list.right, area_bottom);
    for (i = 0; i < lrc_count(); i++) {
        int y = cy + (i - cur) * S(30);
        if (y < area_top - S(30) || y > area_bottom + S(10))
            continue;
        r.left = L.list.left + S(10);
        r.right = L.list.right - S(10);
        r.top = y - S(12);
        r.bottom = r.top + S(24);
        ui_text(dc, lrc_text(i), -1, &r,
                i == cur ? C_ACCENT : (i < cur ? C_TEXT3 : C_TEXT2),
                i == cur ? ui_font(14, FW_BOLD) : ui_font(12, FW_NORMAL),
                DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    RestoreDC(dc, saved);
}

static void paint(HDC dc, int W, int H)
{
    int pad = S(28);

    layout(W, H);
    if (g_mini) {
        draw_mini(dc, W, H);
        draw_toast(dc, W, H);
        return;
    }
    if (g_settings) {
        draw_settings(dc, W, H);
        return;
    }
    ui_fill(dc, 0, 0, W, H, C_BG);

    if (g_fast_bar) {
        /* dragging the seek knob and only the bar is dirty */
        draw_bottom(dc, W, H);
        draw_toast(dc, W, H);
        return;
    }

    {
        HBRUSH b;
        HBRUSH ob;
        HPEN op;
        b = CreateSolidBrush(RGB(0x3F, 0xC9, 0x96));
        ob = (HBRUSH)SelectObject(dc, b);
        op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, pad, S(17), pad + S(30), S(47), S(9), S(9));
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(b);
        ui_glyph(dc, GLYPH_NOTE, pad + S(5), S(20), S(20), RGB(0x0B, 0x18, 0x13));
        ui_text_at(dc, L"拾音", -1, pad + S(42), S(14), C_TEXT, ui_font(16, FW_BOLD));
        ui_text_at(dc, L"SHIYIN 1.0  ·  \u672c\u5730\u97f3\u4e50\u5e93", -1, pad + S(43), S(38), C_TEXT3,
                   ui_font(9, FW_NORMAL));
    }

    draw_pill_button(dc, &L.addf, g_hover == HOV_ADDF, 1, L"添加文件", GLYPH_PLUS);
    draw_pill_button(dc, &L.addfolder, g_hover == HOV_FOLDER, 0, L"文件夹", GLYPH_FOLDER);
    draw_pill_button(dc, &L.lyr, g_hover == HOV_LYR, g_lyrics, L"歌词", 0);
    draw_gear(dc, &L.gear, g_hover == HOV_GEAR);
    ui_line(dc, 0, S(TOP_H), W, S(TOP_H), C_LINE_SOFT);

    draw_cover(dc);
    draw_info(dc);
    if (g_lyrics)
        draw_lyrics(dc, W, H);
    else
        draw_playlist(dc);
    draw_bottom(dc, W, H);
    draw_toast(dc, W, H);
}

/* ------------------------------------------------------------------ */
/*  input                                                              */
/* ------------------------------------------------------------------ */

static int row_at(int y, int *out_idx)
{
    int rel;
    if (y < L.rows_top || y > L.rows_top + L.view_h)
        return 0;
    rel = y - L.rows_top + g_scroll;
    if (rel < 0)
        return 0;
    rel /= L.row_h;
    if (rel < 0 || rel >= g_view_n)
        return 0;
    *out_idx = g_view[rel];
    return 1;
}

static void seek_preview_from_x(int x)
{
    int l = L.seek.left, r = L.seek.right, ms;

    if (r <= l || g_dur <= 0)
        return;
    if (x < l) x = l;
    if (x > r) x = r;
    ms = (x - l) * g_dur / (r - l);
    if (ms < 0) ms = 0;
    if (ms > g_dur) ms = g_dur;
    {
        DWORD now = GetTickCount();
        g_seek_preview = ms;
        g_pos = ms;
        if (now - g_preview_tick < 16)
            return;
        g_preview_tick = now;
    }
    invalidate_bar();
}

static void seek_from_x(int x)
{
    int l = L.seek.left, r = L.seek.right;
    int ms;
    if (r <= l || g_dur <= 0)
        return;
    if (x < l) x = l;
    if (x > r) x = r;
    ms = (int)((long long)(x - l) * g_dur / (r - l));
    seek_to(ms);
}

static void volume_from_x(int x)
{
    int l = L.vol.left, r = L.vol.right;
    int v;
    if (r <= l)
        return;
    if (x < l) x = l;
    if (x > r) x = r;
    v = (x - l) * 100 / (r - l);
    set_volume(v);
}

/* A low integrity process cannot raise itself, but Explorer runs at medium
 * integrity: asking it to start our exe gives the new instance normal rights. */
static void relaunch_via_shell(void)
{
    char exe[SY_PATH];
    SHELLEXECUTEINFOA sei;

    GetModuleFileNameA(NULL, exe, SY_PATH);
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    /* Let the shell (which runs at medium integrity) start the new instance:
     * a low integrity process cannot raise its own token. */
    sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = "open";
    sei.lpFile = exe;
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExA(&sei)) {
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }
    {
        /* The shell refused (sandboxed session): at least put the user in the
         * right folder so one double-click is all that is left. */
        char arg[SY_PATH + 32];
        arg[0] = 0;
        strncat(arg, "/select,\"", sizeof(arg) - 1);
        strncat(arg, exe, sizeof(arg) - strlen(arg) - 1);
        strncat(arg, "\"", sizeof(arg) - strlen(arg) - 1);
        ShellExecuteA(NULL, "open", "explorer.exe", arg, NULL, SW_SHOWNORMAL);
        toast(L"已打开所在文件夹，请双击 Shiyin.exe");
        invalidate_all();
    }
}

/* ------------------------------------------------------------------ */
/*  system tray                                                        */
/* ------------------------------------------------------------------ */

/* Windows 95 only knows the version 1 layout (88 bytes, tip 64 bytes), so a
 * dedicated struct is used instead of the modern NOTIFYICONDATA. */
static void menu_str(HMENU m, UINT flags, UINT_PTR id, const wchar_t *text);
static void toggle_play(void);
static void prev_track(void);
static void next_track(int auto_advance);
static void tray_hide(void);

typedef struct {
    DWORD cbSize;
    HWND  hWnd;
    UINT  uID;
    UINT  uFlags;
    UINT  uCallbackMessage;
    HICON hIcon;
    char  szTip[64];
} NID95;

static NID95 g_nid;
static int   g_tray_added;
static int   g_tray_tries;
static UINT  g_taskbar_msg;

static void tray_add(void)
{
    HMODULE sh;
    BOOL (WINAPI *pfn)(DWORD, void *);

    if (g_tray_added)
        return;
    sh = GetModuleHandleA("shell32.dll");
    if (!sh)
        return;
    pfn = (BOOL (WINAPI *)(DWORD, void *))GetProcAddress(sh, "Shell_NotifyIconA");
    if (!pfn)
        return;

    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);      /* 88 */
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = 0x1 | 0x2 | 0x4;    /* MESSAGE | ICON | TIP */
    g_nid.uCallbackMessage = SY_WM_TRAY;
    g_nid.hIcon = LoadIconA(g_inst, (LPCSTR)1);
    if (!g_nid.hIcon)
        g_nid.hIcon = LoadIconA(NULL, (LPCSTR)32512);   /* IDI_APPLICATION */
    w_ansi(L"拾音 · Shiyin", g_nid.szTip, (int)sizeof(g_nid.szTip));

    g_tray_added = pfn(0 /* NIM_ADD */, &g_nid) ? 1 : 0;
    sy_log("tray_add", g_tray_added, (int)GetLastError());
}

static void tray_remove(void)
{
    HMODULE sh;
    BOOL (WINAPI *pfn)(DWORD, void *);
    if (!g_tray_added)
        return;
    sh = GetModuleHandleA("shell32.dll");
    if (sh) {
        pfn = (BOOL (WINAPI *)(DWORD, void *))GetProcAddress(sh, "Shell_NotifyIconA");
        if (pfn)
            pfn(2 /* NIM_DELETE */, &g_nid);
    }
    g_tray_added = 0;
}

static void tray_show_window(void)
{
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

static void tray_menu(void)
{
    HMENU m = CreatePopupMenu();
    POINT pt;
    int cmd;

    if (!m)
        return;
    GetCursorPos(&pt);
    menu_str(m, MF_STRING, 1, g_hwnd && IsWindowVisible(g_hwnd) ? L"隐藏窗口" : L"显示窗口");
    menu_str(m, MF_SEPARATOR, 0, L"");
    menu_str(m, MF_STRING, 2, g_active && !g_paused ? L"暂停" : L"播放");
    menu_str(m, MF_STRING, 3, L"上一首");
    menu_str(m, MF_STRING, 4, L"下一首");
    menu_str(m, MF_SEPARATOR, 0, L"");
    menu_str(m, MF_STRING, 5, L"退出");
    SetForegroundWindow(g_hwnd);
    cmd = (int)TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                              pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(m);
    PostMessage(g_hwnd, WM_NULL, 0, 0);
    switch (cmd) {
    case 1:
        if (IsWindowVisible(g_hwnd))
            tray_hide();
        else
            tray_show_window();
        break;
    case 2:
        toggle_play();
        break;
    case 3:
        prev_track();
        break;
    case 4:
        next_track(0);
        break;
    case 5:
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

static void tray_hide(void)
{
    if (!g_tray_added) {
        toast(L"系统托盘不可用，无法隐藏窗口");
        return;
    }
    ShowWindow(g_hwnd, SW_HIDE);
}

static void mini_seek_from_x(int x)
{
    int l = m_seek.left, r = m_seek.right;
    if (r <= l || g_dur <= 0) {
        g_mini_drag_ms = 0;
        return;
    }
    if (x < l) x = l;
    if (x > r) x = r;
    g_mini_drag_ms = (x - l) * g_dur / (r - l);
    if (g_mini_drag_ms < 0) g_mini_drag_ms = 0;
    if (g_mini_drag_ms > g_dur) g_mini_drag_ms = g_dur;
    invalidate_all();
}

static void enter_mini(void)
{
    RECT old, sr;
    int w, h;
    int sw = GetSystemMetrics(0), sh = GetSystemMetrics(1);

    if (g_mini_w < S(240)) g_mini_w = S(380);
    if (g_mini_h < S(84)) g_mini_h = S(132);
    w = g_mini_w;
    h = g_mini_h;
    if (w > sw - S(40)) w = sw - S(40);
    if (h > sh - S(80)) h = sh - S(80);

    GetWindowRect(g_hwnd, &old);
    if (!g_mini)
        g_full_rect = old;
    g_mini = 1;
    /* No style changes: SetWindowLong makes Windows 95 leave the old frame
     * pixels behind.  Just move/resize and force the desktop to repaint the
     * rectangle we are leaving. */
    SetWindowPos(g_hwnd, g_mini_top ? HWND_TOPMOST : HWND_TOP, sw - w - S(24), sh - h - S(70), w, h,
                 SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    InvalidateRect(NULL, &old, TRUE);
    GetWindowRect(g_hwnd, &sr);
    InvalidateRect(NULL, &sr, TRUE);
    UpdateWindow(g_hwnd);
    invalidate_all();
}

static void leave_mini(void)
{
    RECT old;
    GetWindowRect(g_hwnd, &old);
    g_mini = 0;
    SetWindowPos(g_hwnd, HWND_NOTOPMOST,
                 g_full_rect.left, g_full_rect.top,
                 g_full_rect.right - g_full_rect.left,
                 g_full_rect.bottom - g_full_rect.top,
                 SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    InvalidateRect(NULL, &old, TRUE);
    UpdateWindow(g_hwnd);
    invalidate_all();
}

static char *next_cmd_arg(char *p, char *out, int cap);
static int is_playlist_file(const char *path);
static int playlist_import(const char *path);

static void copy_to_clipboard(const char *text)
{
    HGLOBAL h;
    char *p;
    if (!text || !OpenClipboard(g_hwnd))
        return;
    EmptyClipboard();
    h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)strlen(text) + 1);
    if (h) {
        p = (char *)GlobalLock(h);
        if (p) {
            strcpy(p, text);
            GlobalUnlock(h);
            SetClipboardData(CF_TEXT, h);
            h = 0;
        }
    }
    if (h)
        GlobalFree(h);
    CloseClipboard();
}

static void reveal_in_explorer(const char *path)
{
    char arg[SY_PATH + 32];
    arg[0] = 0;
    strncat(arg, "/select,\"", sizeof(arg) - 1);
    strncat(arg, path, sizeof(arg) - strlen(arg) - 1);
    strncat(arg, "\"", sizeof(arg) - strlen(arg) - 1);
    ShellExecuteA(NULL, "open", "explorer.exe", arg, NULL, SW_SHOWNORMAL);
}

/* Add every path found in "text" (an argument string); returns the first one
 * that ended up in the playlist, so callers can start playback. */
static int add_files_from_text(char *text)
{
    char path[SY_PATH];
    char *p = text;
    int first = -1;

    for (;;) {
        int idx;
        p = next_cmd_arg(p, path, sizeof(path));
        if (!path[0])
            break;
        if (is_playlist_file(path)) {
            playlist_import(path);
            continue;
        }
        if (!file_exists(path) || is_dir(path))
            continue;
        idx = list_find(&g_list, path);
        if (idx < 0) {
            int before = g_list.count;
            add_path(path);
            if (g_list.count > before)
                idx = g_list.count - 1;
        }
        if (idx >= 0 && first < 0)
            first = idx;
    }
    return first;
}

static int recent_get(const char *path)
{
    int i;
    for (i = 0; i < RECENT_N; i++)
        if (g_recent_path[i][0] && !strcmp(g_recent_path[i], path))
            return g_recent_ms[i];
    return 0;
}

static void recent_set(const char *path, int ms)
{
    int i, slot = -1, oldest = 0;
    for (i = 0; i < RECENT_N; i++) {
        if (g_recent_path[i][0] && !strcmp(g_recent_path[i], path)) {
            g_recent_ms[i] = ms;
            g_recent_seq[i] = ++g_recent_tick;
            return;
        }
        if (!g_recent_path[i][0] && slot < 0)
            slot = i;
        if (g_recent_seq[i] < g_recent_seq[oldest])
            oldest = i;
    }
    if (slot < 0)
        slot = oldest;                  /* least recently used slot */
    strncpy(g_recent_path[slot], path, SY_PATH - 1);
    g_recent_path[slot][SY_PATH - 1] = 0;
    g_recent_ms[slot] = ms;
    g_recent_seq[slot] = ++g_recent_tick;
}

/* ReplayGain: scan the tag block for REPLAYGAIN_TRACK_GAIN.  Both Vorbis
 * comments and ID3v2 TXXX frames store the key as literal ASCII, so a byte
 * scan avoids touching either parser.  Returns hundredths of a dB. */
static int replaygain_db100(const char *path)
{
    SyFile f;
    const char *key = "REPLAYGAIN_TRACK_GAIN";
    int kl = 22, i, n, val = 0;

    if (!file_map(path, &f))
        return 0;
    n = (int)f.size;
    {
        const char *b = (const char *)f.base;
        for (i = 0; i + kl + 4 < n; i++) {
            int k;
            for (k = 0; k < kl; k++) {
                char c = b[i + k];
                if (c >= 'a' && c <= 'z')
                    c = (char)(c - 32);
                if (c != key[k])
                    break;
            }
            if (k == kl) {
                int j = i + kl;
                while (j < n && b[j] != '=' && b[j] != ':')
                    j++;
                if (j < n) {
                    int sign = 1, whole = 0, frac = 0, fd = 0;
                    j++;
                    while (j < n && (b[j] == ' ' || b[j] == '"'))
                        j++;
                    if (j < n && (b[j] == '-' || b[j] == '+')) {
                        if (b[j] == '-')
                            sign = -1;
                        j++;
                    }
                    while (j < n && b[j] >= '0' && b[j] <= '9') {
                        whole = whole * 10 + (b[j] - '0');
                        j++;
                    }
                    if (j < n && b[j] == '.') {
                        j++;
                        while (j < n && b[j] >= '0' && b[j] <= '9' && fd < 2) {
                            frac = frac * 10 + (b[j] - '0');
                            j++;
                            fd++;
                        }
                    }
                    while (fd < 2) {
                        frac *= 10;
                        fd++;
                    }
                    val = sign * (whole * 100 + frac);
                }
                break;
            }
        }
    }
    file_unmap(&f);
    if (val < -2400 || val > 1200)
        return 0;
    return val;
}

/* the track that follows idx under the current playback mode */
static int next_index_for(int idx)
{
    int n = g_list.count;
    if (n <= 0 || idx < 0 || idx >= n)
        return -1;
    switch (g_mode) {
    case MODE_ONE:
        return idx;
    case MODE_ALL:
        return (idx + 1) % n;
    case MODE_SHUFFLE:
        return n > 1 ? random_other() : idx;
    default:
        return idx + 1 < n ? idx + 1 : -1;
    }
}

static void open_settings(void)
{
    g_settings = 1;
    g_assoc_on = assoc_is_default();
    g_assoc_err = 0;
    invalidate_all();
}

static void toggle_assoc(void)
{
    if (g_assoc_on) {
        assoc_remove();
        g_assoc_on = 0;
        g_assoc_err = 0;
        g_assoc_note = 0;
        toast(L"已取消默认打开方式");
    } else {
        int rc = assoc_set_default();
        if (rc == ASSOC_NEEDS_UI) {
            /* Windows 8+ : we registered, the user picks in system settings */
            g_assoc_on = 0;
            g_assoc_err = 0;
            g_assoc_note = 1;
            toast(L"请在系统设置中选择拾音");
        } else if (rc) {
            g_assoc_err = rc;
            g_assoc_note = 0;
            toast(rc == 5 && assoc_integrity_low() ? L"权限受限，请从资源管理器运行"
                                                   : L"写入注册表失败");
        } else {
            g_assoc_on = 1;
            g_assoc_err = 0;
            g_assoc_note = 0;
            toast(L"已设为默认打开方式");
        }
    }
    invalidate_all();
}

static void on_down(int x, int y)
{
    int idx;
    SetFocus(g_hwnd);

    if (!hit(&L.search, x, y) && g_search_on) {
        g_search_on = 0;
        invalidate_all();
    }
    if (g_settings) {
        if (hit(&L.back, x, y)) {
            g_settings = 0;
            invalidate_all();
            return;
        }
        if (g_low_integrity && hit(&g_relaunch_btn, x, y)) {
            relaunch_via_shell();
            return;
        }
        if (x >= g_assoc_row.left && x <= g_assoc_row.right &&
            y >= g_assoc_row.top && y <= g_assoc_row.bottom) {
            toggle_assoc();
            return;
        }
        if (x >= g_close_row.left && x <= g_close_row.right &&
            y >= g_close_row.top && y <= g_close_row.bottom) {
            g_close_to_tray = !g_close_to_tray;
            g_dirty = 1;
            toast(g_close_to_tray ? L"关闭窗口将收进托盘" : L"关闭窗口将退出程序");
            invalidate_all();
            return;
        }
        if (x >= g_theme_row.left && x <= g_theme_row.right &&
            y >= g_theme_row.top && y <= g_theme_row.bottom) {
            g_theme = (g_theme + 1) % 3;
            theme_apply();
            apply_title_theme();
            g_theme_check = GetTickCount();
            g_dirty = 1;
            invalidate_all();
            return;
        }
        if (x >= g_lang_row.left && x <= g_lang_row.right &&
            y >= g_lang_row.top && y <= g_lang_row.bottom) {
            g_lang = g_lang ? 0 : 1;
            g_dirty = 1;
            {
                char t[128];
                w_ansi(tr(L"拾音 · Shiyin"), t, sizeof(t));
                SetWindowTextA(g_hwnd, t);
            }
            invalidate_all();
            return;
        }
        if (x >= g_gapless_row.left && x <= g_gapless_row.right &&
            y >= g_gapless_row.top && y <= g_gapless_row.bottom) {
            g_gapless_on = !g_gapless_on;
            g_dirty = 1;
            if (!g_gapless_on)
                audio_set_next("");
            toast(g_gapless_on ? L"已开启无缝衔接" : L"已关闭无缝衔接");
            invalidate_all();
            return;
        }
        if (x >= g_fade_row.left && x <= g_fade_row.right &&
            y >= g_fade_row.top && y <= g_fade_row.bottom) {
            g_fade_on = !g_fade_on;
            audio_set_fade(g_fade_on);
            g_dirty = 1;
            toast(g_fade_on ? L"已开启淡入淡出" : L"已关闭淡入淡出");
            invalidate_all();
            return;
        }
        if (x >= g_top_row.left && x <= g_top_row.right &&
            y >= g_top_row.top && y <= g_top_row.bottom) {
            g_mini_top = !g_mini_top;
            g_dirty = 1;
            if (g_mini)
                SetWindowPos(g_hwnd, g_mini_top ? HWND_TOPMOST : HWND_TOP,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            toast(g_mini_top ? L"迷你窗口将置顶" : L"迷你窗口不再置顶");
            invalidate_all();
            return;
        }
        if (x >= g_mini_row.left && x <= g_mini_row.right &&
            y >= g_mini_row.top && y <= g_mini_row.bottom) {
            g_mini_on_min = !g_mini_on_min;
            g_dirty = 1;
            toast(g_mini_on_min ? L"最小化后显示迷你模式" : L"最小化后不显示迷你模式");
            invalidate_all();
            return;
        }
        if (x >= g_tray_row.left && x <= g_tray_row.right &&
            y >= g_tray_row.top && y <= g_tray_row.bottom) {
            g_min_to_tray = !g_min_to_tray;
            g_dirty = 1;
            toast(g_min_to_tray ? L"最小化后将隐藏到托盘" : L"最小化后将留在任务栏");
            invalidate_all();
            return;
        }
        return;
    }
    if (g_mini) {
        if (hit(&m_restore, x, y)) { leave_mini(); return; }
        if (x >= m_seek.left - S(8) && x <= m_seek.right + S(8) &&
            y >= m_seek.top - S(14) && y <= m_seek.bottom + S(14)) {
            g_drag = 3;
            g_mini_dragging = 1;
            mini_seek_from_x(x);
            return;
        }
        if (hit(&m_prev, x, y)) { prev_track(); return; }
        if (hit(&m_play, x, y)) { toggle_play(); return; }
        if (hit(&m_next, x, y)) { next_track(0); return; }
        return;
    }
    if (hit(&L.lyr, x, y)) {
        g_lyrics = !g_lyrics;
        if (g_lyrics && g_sel >= 0)
            lrc_load(g_list.items[g_sel].path);
        invalidate_all();
        return;
    }
    if (hit(&L.gear, x, y)) { open_settings(); return; }
    if (hit(&L.addf, x, y)) { open_files(); return; }
    if (hit(&L.addfolder, x, y)) { open_folder(); return; }
    if (hit(&L.search, x, y)) {
        g_search_on = 1;
        g_caret = 1;
        invalidate_all();
        return;
    }
    if (g_list.count == 0 && hit(&L.empty_btn, x, y)) { open_files(); return; }
    if (hit(&L.play, x, y)) { toggle_play(); return; }
    if (hit(&L.prev, x, y)) { prev_track(); return; }
    if (hit(&L.next, x, y)) { next_track(0); return; }
    if (hit(&L.mode, x, y)) { cycle_mode(); return; }
    if (x >= L.seek.left - S(10) && x <= L.seek.right + S(10) &&
        y >= L.seek.top - S(10) && y <= L.seek.bottom + S(10)) {
        /* pressing the bar must not seek yet: that would flush the buffers and
         * silence the music for ~0.5 s.  Only the release commits. */
        g_drag = 1;
        g_preview_tick = 0;
        seek_preview_from_x(x);
        return;
    }
    if (x >= L.vol.left - S(10) && x <= L.vol.right + S(10) &&
        y >= L.vol.top - S(12) && y <= L.vol.bottom + S(12)) {
        g_drag = 2;
        volume_from_x(x);
        return;
    }
    if (hit(&L.hdr_title, x, y)) { set_sort(1); return; }
    if (hit(&L.hdr_album, x, y)) { set_sort(3); return; }
    if (hit(&L.hdr_time, x, y)) { set_sort(4); return; }
    if (row_at(y, &idx)) {
        int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        mark_ensure();
        if (shift && g_anchor >= 0) {
            int a = g_anchor < idx ? g_anchor : idx;
            int b = g_anchor < idx ? idx : g_anchor;
            int k;
            mark_clear();
            for (k = a; k <= b; k++)
                g_mark[k] = 1;
        } else if (ctrl) {
            g_mark[idx] = g_mark[idx] ? 0 : 1;
            g_anchor = idx;
        } else {
            mark_clear();
            g_mark[idx] = 1;
            g_anchor = idx;
        }
        g_sel = idx;
        g_press_row = idx;
        g_press_y = y;
        sync_art();
        sync_lyrics();
        invalidate_all();
        return;
    }
}

/* How many rows a given view slot moves out of the way while dragging. */
static int row_shift(int i)
{
    if (g_drag_row < 0 || g_from_pos < 0 || g_drop_pos < 0)
        return 0;
    if (i == g_from_pos)
        return 0;
    if (g_drop_pos > g_from_pos) {
        if (i > g_from_pos && i < g_drop_pos)
            return -1;
    } else if (g_drop_pos < g_from_pos) {
        if (i >= g_drop_pos && i < g_from_pos)
            return 1;
    }
    return 0;
}

/* 0..256 ease-out cubic */
static int ease_shift(void)
{
    int t = g_anim_t, inv;
    if (t > 256) t = 256;
    inv = 256 - t;
    return 256 - ((inv * inv * inv) >> 16);
}

static void drag_update(int y)
{
    int rel, pos;
    if (g_drag_row < 0)
        return;
    rel = y - L.rows_top + g_scroll;
    pos = rel / (L.row_h > 0 ? L.row_h : 1);
    if (pos < 0) pos = 0;
    if (pos > g_view_n) pos = g_view_n;
    if (pos != g_drop_pos) {
        g_drop_pos = pos;
        g_anim_t = 0;
        SetTimer(g_hwnd, 2, 16, NULL);
    }
}

static void drag_commit(void)
{
    int from, to;

    if (g_drag_row < 0) {
        g_press_row = -1;
        return;
    }
    from = view_pos_of(g_drag_row);
    to = g_drop_pos;
    g_drag_row = -1;
    g_drop_pos = -1;
    g_from_pos = -1;
    g_anim_t = 256;
    g_press_row = -1;
    KillTimer(g_hwnd, 2);
    if (from >= 0 && to >= 0) {
        int target = to > from ? to - 1 : to;
        move_track(from, target);
    }
    invalidate_all();
}

static void on_move(int x, int y)
{
    int h = HOV_NONE;
    int i;

    if (g_drag == 3) { mini_seek_from_x(x); return; }
    if (g_drag == 1) { seek_preview_from_x(x); return; }
    if (g_drag == 2) { volume_from_x(x); return; }
    if (g_press_row >= 0 && (GetKeyState(VK_LBUTTON) & 0x8000)) {
        if (g_drag_row < 0) {
            int dy = y - g_press_y;
            if (dy < 0) dy = -dy;
            if (dy > S(4)) {
                if (g_sort != 0) {
                    toast(L"拖拽排序前请先恢复手动顺序");
                    g_press_row = -1;
                } else if (g_search[0]) {
                    toast(L"搜索时不能拖拽排序");
                    g_press_row = -1;
                } else {
                    g_drag_row = g_press_row;
                    g_from_pos = view_pos_of(g_drag_row);
                    g_drop_pos = g_from_pos;
                    g_anim_t = 256;
                    if (g_drop_pos < 0) g_drop_pos = 0;
                }
            }
        }
        if (g_drag_row >= 0) {
            drag_update(y);
            g_drag_y = y;
            invalidate_all();
            return;
        }
    }

    if (g_settings) {
        h = 0;
        if (x >= g_assoc_row.left && x <= g_assoc_row.right &&
            y >= g_assoc_row.top && y <= g_assoc_row.bottom)
            h = HOV_ADDF;
        else if (x >= g_tray_row.left && x <= g_tray_row.right &&
                 y >= g_tray_row.top && y <= g_tray_row.bottom)
            h = HOV_GEAR;
        else if (hit(&L.back, x, y))
            h = HOV_FOLDER;
        else if (g_low_integrity && hit(&g_relaunch_btn, x, y))
            h = HOV_GEAR;
    } else if (hit(&L.lyr, x, y)) h = HOV_LYR;
    else if (hit(&L.gear, x, y)) h = HOV_GEAR;
    else if (hit(&L.addf, x, y)) h = HOV_ADDF;
    else if (hit(&L.addfolder, x, y)) h = HOV_FOLDER;
    else if (hit(&L.play, x, y)) h = HOV_GLYPH(GLYPH_PLAY);
    else if (hit(&L.prev, x, y)) h = HOV_GLYPH(GLYPH_PREV);
    else if (hit(&L.next, x, y)) h = HOV_GLYPH(GLYPH_NEXT);
    else if (hit(&L.mode, x, y)) h = HOV_MODE;
    else if (hit(&L.empty_btn, x, y) && g_list.count == 0) h = HOV_ADDF;
    else if (row_at(y, &i)) h = i;

    if (h != g_hover) {
        g_hover = h;
        invalidate_all();
    }
}

static void on_wheel(int delta)
{
    int maxs;
    if (delta == 0)
        return;
    g_scroll -= (delta / 120) * L.row_h * 2;
    maxs = g_view_n * L.row_h - L.view_h;
    if (maxs < 0)
        maxs = 0;
    if (g_scroll > maxs)
        g_scroll = maxs;
    if (g_scroll < 0)
        g_scroll = 0;
    invalidate_all();
}

static void on_key(int vk)
{
    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (g_settings) {
        if (vk == VK_ESCAPE) {
            g_settings = 0;
            invalidate_all();
        }
        return;
    }
    if (g_search_on) {
        if (vk == VK_ESCAPE) {
            g_search_on = 0;
            g_search[0] = 0;
            rebuild_view();
            invalidate_all();
        } else if (vk == VK_RETURN) {
            g_search_on = 0;
            if (g_view_n > 0)
                play_track(g_view[0]);
            invalidate_all();
        } else if (vk == VK_BACK) {
            int n = (int)strlen(g_search);
            if (n > 0)
                g_search[n - 1] = 0;
            rebuild_view();
            invalidate_all();
        }
        return;
    }

    switch (vk) {
    case VK_SPACE:
        toggle_play();
        break;
    case VK_LEFT:
        if (ctrl) prev_track(); else seek_to(g_pos - 5000);
        break;
    case VK_RIGHT:
        if (ctrl) next_track(0); else seek_to(g_pos + 5000);
        break;
    case VK_UP:
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) { move_marked(-1); break; }
        if (g_view_n > 0) {
            int p = view_pos_of(g_sel);
            p = (p <= 0) ? 0 : p - 1;
            g_sel = g_view[p];
            ensure_visible(g_sel);
            invalidate_all();
        }
        break;
    case VK_DOWN:
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) { move_marked(1); break; }
        if (g_view_n > 0) {
            int p = view_pos_of(g_sel);
            p = (p < 0 || p + 1 >= g_view_n) ? g_view_n - 1 : p + 1;
            g_sel = g_view[p];
            ensure_visible(g_sel);
            invalidate_all();
        }
        break;
    case VK_HOME:
        if (g_view_n > 0) { g_sel = g_view[0]; g_scroll = 0; invalidate_all(); }
        break;
    case VK_END:
        if (g_view_n > 0) { g_sel = g_view[g_view_n - 1]; ensure_visible(g_sel); invalidate_all(); }
        break;
    case VK_RETURN:
        if (g_sel >= 0) play_track(g_sel);
        break;
    case VK_DELETE:
        remove_selected();
        break;
    case 'O':
        if (ctrl) open_files();
        break;
    case 'D':
        if (ctrl) open_folder();
        break;
    case 'F':
        if (ctrl) { g_search_on = 1; g_caret = 1; invalidate_all(); }
        break;
    case 'M':
        if (ctrl) {
            if (g_vol > 0) { g_last_vol = g_vol; set_volume(0); }
            else set_volume(g_last_vol ? g_last_vol : 70);
        }
        break;
    case 'N':
        if (ctrl) next_track(0);
        break;
    case 'P':
        if (ctrl) prev_track();
        break;
    case 'R':
        if (!ctrl) cycle_mode();
        break;
    case 'L':
        if (!ctrl) {
            g_lyrics = !g_lyrics;
            if (g_lyrics)
                sync_lyrics();
            invalidate_all();
        }
        break;
    default:
        break;
    }
}

static void on_char(unsigned int ch)
{
    int n;
    if (!g_search_on)
        return;
    if (ch < 32 || ch == 127)
        return;
    n = (int)strlen(g_search);
    if (n < (int)sizeof(g_search) - 3) {
        g_search[n] = (char)ch;
        g_search[n + 1] = 0;
    }
    rebuild_view();
    invalidate_all();
}

/* ------------------------------------------------------------------ */
/*  playlists (m3u / m3u8 / pls)                                       */
/* ------------------------------------------------------------------ */

static int is_playlist_file(const char *path)
{
    return path_has_ext(path, ".m3u") || path_has_ext(path, ".m3u8") ||
           path_has_ext(path, ".pls");
}

static int playlist_import(const char *path)
{
    SyFile f;
    char dir[SY_PATH], line[SY_PATH], full[SY_PATH];
    int i = 0, n, added = 0;

    if (!file_map(path, &f))
        return -1;
    path_dir(path, dir, sizeof(dir));
    n = (int)f.size;

    while (i < n) {
        int len = 0;
        char *src = (char *)f.base;
        /* skip the UTF-8 BOM once */
        if (i == 0 && n >= 3 && (unsigned char)src[0] == 0xEF &&
            (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
            i = 3;
        while (i < n && (src[i] == '\r' || src[i] == '\n'))
            i++;
        while (i < n && src[i] != '\r' && src[i] != '\n' && len < SY_PATH - 1)
            line[len++] = src[i++];
        line[len] = 0;
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t'))
            line[--len] = 0;
        if (!len || line[0] == '#' || line[0] == '[')
            continue;
        if ((line[0] == 'F' || line[0] == 'f') && line[1] == 'i') {
            /* PLS: FileN=path */
            int k = 0, eq = -1;
            while (line[k] && line[k] != '=')
                k++;
            if (line[k] == '=')
                eq = k;
            if (eq < 0)
                continue;
            strcpy(line, line + eq + 1);
        }
        if (line[1] == ':')
            strcpy(full, line);
        else
            path_join(full, sizeof(full), dir, line);
        if (file_exists(full)) {
            add_path(full);
            added++;
        }
    }
    file_unmap(&f);
    return added;
}

static void playlist_export(void)
{
    static char path[SY_PATH];
    static char filter[192];
    OPENFILENAMEA ofn;
    HANDLE h;
    DWORD wr;
    int i, n;

    {
        char *q = filter;
        int k;
        static const wchar_t *parts[] = {
            L"M3U \u64ad\u653e\u5217\u8868 (*.m3u)", L"*.m3u", 0
        };
        for (k = 0; parts[k]; k++) {
            w_ansi(parts[k], q, (int)(sizeof(filter) - (size_t)(q - filter)));
            q += strlen(q) + 1;
        }
        *q = 0;
    }
    memset(&ofn, 0, sizeof(ofn));
    path[0] = 0;
    ofn.lStructSize = 76;
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = SY_PATH;
    ofn.lpstrDefExt = "m3u";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn))
        return;

    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        toast(L"无法写入播放列表");
        return;
    }
    n = 10;
    WriteFile(h, "#EXTM3U\r\n", (DWORD)n, &wr, NULL);
    for (i = 0; i < g_list.count; i++) {
        n = (int)strlen(g_list.items[i].path);
        WriteFile(h, g_list.items[i].path, (DWORD)n, &wr, NULL);
        WriteFile(h, "\r\n", 2, &wr, NULL);
    }
    CloseHandle(h);
    toast(L"播放列表已导出");
}

static void on_drop(HDROP drop)
{
    UINT n = DragQueryFileA(drop, 0xFFFFFFFF, NULL, 0);
    UINT i;
    for (i = 0; i < n; i++) {
        char path[SY_PATH];
        if (!DragQueryFileA(drop, i, path, SY_PATH))
            continue;
        if (is_playlist_file(path)) {
            int got = playlist_import(path);
            if (got < 0)
                toast(L"无法读取播放列表");
        } else {
            add_path(path);
        }
    }
    DragFinish(drop);
}

/* ------------------------------------------------------------------ */
/*  window                                                             */
/* ------------------------------------------------------------------ */

static void do_resize(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (g_mdc) { DeleteDC(g_mdc); g_mdc = 0; }
    if (g_mbmp) { DeleteObject(g_mbmp); g_mbmp = 0; }
    g_mw = rc.right;
    g_mh = rc.bottom;
    if (g_mw > 0 && g_mh > 0) {
        HDC dc = GetDC(hwnd);
        g_mdc = CreateCompatibleDC(dc);
        if (g_mdc) {
            g_mbmp = CreateCompatibleBitmap(dc, g_mw, g_mh);
            if (g_mbmp)
                SelectObject(g_mdc, g_mbmp);
        }
        ReleaseDC(hwnd, dc);
    }
    layout(g_mw, g_mh);
}

/* Split one quoted or bare argument off the command line. */
static char *next_cmd_arg(char *p, char *out, int cap)
{
    int n = 0;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && n < cap - 1)
            out[n++] = *p++;
        if (*p == '"')
            p++;
    } else {
        while (*p && *p != ' ' && *p != '\t' && n < cap - 1)
            out[n++] = *p++;
    }
    out[n] = 0;
    return p;
}

/* Files passed on the command line are added to the playlist, so the player
 * can be associated with audio files in Explorer.  The first one is queued
 * for playback once the audio engine exists. */
static int g_autoplay = -1;

/* settings page */
static int g_settings;
static int g_assoc_on;
static int g_assoc_err;
static int g_assoc_note;
static int g_low_integrity;
static RECT g_tray_row;
static RECT g_relaunch_btn;
static RECT g_assoc_row;

static void add_command_line_files(void)
{
    char path[SY_PATH];
    char *p = GetCommandLineA();
    char *rest;
    int added = 0;

    p = next_cmd_arg(p, path, sizeof(path));       /* program name */
    rest = p;
    for (;;) {
        p = next_cmd_arg(p, path, sizeof(path));
        if (!path[0])
            break;
        /* Files only: a mis-parsed argument must never turn into a recursive
         * scan of a drive root (that is what a bare "\" used to do). */
        if (is_playlist_file(path)) {
            playlist_import(path);
            continue;
        }
        if (file_exists(path) && !is_dir(path)) {
            int idx = list_find(&g_list, path);
            wchar_t base[SY_TEXT], msg[SY_TEXT + 32];
            path_base(path, base, SY_TEXT);
            if (idx < 0) {
                int before = g_list.count;
                add_path(path);
                if (g_list.count > before) {
                    idx = g_list.count - 1;
                    w_copy(msg, L"已加入歌单：", SY_TEXT + 32);
                    w_cat(msg, base, SY_TEXT + 32);
                    toast(msg);
                }
            } else {
                w_copy(msg, L"已在歌单中：", SY_TEXT + 32);
                w_cat(msg, base, SY_TEXT + 32);
                toast(msg);
            }
            if (idx >= 0) {
                if (g_autoplay < 0)
                    g_autoplay = idx;
                added++;
            }
        }
    }

    /* Some association commands hand over an unquoted path with spaces, which
     * the tokenizer above splits apart.  If nothing was recognised, treat the
     * whole remainder as a single path. */
    if (added == 0 && rest && *rest) {
        char whole[SY_PATH];
        char *q = rest;
        int n = 0;
        while (*q == ' ' || *q == '\t')
            q++;
        if (*q == '"')
            q++;
        while (*q && *q != '"' && n < SY_PATH - 1)
            whole[n++] = *q++;
        whole[n] = 0;
        while (n > 0 && (whole[n - 1] == ' ' || whole[n - 1] == '\r' || whole[n - 1] == '\n'))
            whole[--n] = 0;
        if (whole[0] && file_exists(whole) && !is_dir(whole)) {
            int idx = list_find(&g_list, whole);
            wchar_t base[SY_TEXT], msg[SY_TEXT + 32];
            path_base(whole, base, SY_TEXT);
            if (idx < 0) {
                int before = g_list.count;
                add_path(whole);
                if (g_list.count > before) {
                    idx = g_list.count - 1;
                    w_copy(msg, L"已加入歌单：", SY_TEXT + 32);
                    w_cat(msg, base, SY_TEXT + 32);
                    toast(msg);
                }
            } else {
                w_copy(msg, L"已在歌单中：", SY_TEXT + 32);
                w_cat(msg, base, SY_TEXT + 32);
                toast(msg);
            }
            if (idx >= 0 && g_autoplay < 0)
                g_autoplay = idx;
        }
    }
}

static void load_settings(void)
{
    g_vol = ini_get_int(g_ini, "ui", "volume", 70);
    if (g_vol < 0) g_vol = 0;
    if (g_vol > 100) g_vol = 100;
    g_last_vol = g_vol ? g_vol : 70;
    g_mode = ini_get_int(g_ini, "ui", "mode", -1);
    if (g_mode < 0 || g_mode >= MODE_COUNT) {
        /* migrate the old, confusing shuffle/repeat pair */
        int sh = ini_get_int(g_ini, "ui", "shuffle", 0);
        int rp = ini_get_int(g_ini, "ui", "repeat", 0);
        if (sh)
            g_mode = MODE_SHUFFLE;
        else if (rp == 2)
            g_mode = MODE_ONE;
        else if (rp == 1)
            g_mode = MODE_ALL;
        else
            g_mode = MODE_ORDER;
    }
    g_min_to_tray = ini_get_int(g_ini, "ui", "mintray", 1);
    g_mini_on_min = ini_get_int(g_ini, "ui", "mini", 1);
    g_close_to_tray = ini_get_int(g_ini, "ui", "closetray", 0);
    g_mini_top = ini_get_int(g_ini, "ui", "mini_top", 1);
    g_fade_on = ini_get_int(g_ini, "ui", "fade", 1);
    g_gapless_on = ini_get_int(g_ini, "ui", "gapless", 1);
    g_theme = ini_get_int(g_ini, "ui", "theme", 0);
    g_lang = ini_get_int(g_ini, "ui", "lang", 0);
    theme_apply();
    g_device = ini_get_int(g_ini, "ui", "device", -1);
    {
        int k;
        char key[16];
        for (k = 0; k < RECENT_N; k++) {
            sprintf_lite(key, sizeof(key), "p%d", k);
            g_recent_path[k][0] = 0;
            ini_get_str(g_ini, "ui", key, "", g_recent_path[k], SY_PATH);
            sprintf_lite(key, sizeof(key), "p%dm", k);
            g_recent_ms[k] = ini_get_int(g_ini, "ui", key, 0);
            g_recent_seq[k] = k;
        }
    }
    g_mini_w = ini_get_int(g_ini, "ui", "miniw", 0);
    g_mini_h = ini_get_int(g_ini, "ui", "minih", 0);
    g_sort = ini_get_int(g_ini, "ui", "sort", 0);
    g_sort_desc = ini_get_int(g_ini, "ui", "sortdir", 0);
    list_load(&g_list, g_ini);
    {
        char last[SY_PATH];
        int idx;
        last[0] = 0;
        ini_get_str(g_ini, "ui", "last", "", last, sizeof(last));
        if (!last[0])
            return;
        idx = list_find(&g_list, last);
        if (idx < 0)
            return;
        g_sel = idx;
        g_resume_idx = idx;
        g_resume_ms = ini_get_int(g_ini, "ui", "pos", 0);
        g_pos = g_resume_ms;
        g_dur = g_list.items[idx].duration_ms;
    }
}

/* Write through a temporary file and swap it in, so a crash mid-write can
 * never leave a truncated ini behind. */
static void save_settings(void)
{
    /* The profile API silently refuses to write when the ini carries a
     * read-only attribute (common for files copied out of an archive), which
     * made the playlist look like it was never saved.  Build the file text
     * ourselves and write it with CreateFile. */
    static char buf[256 * 1024];
    int n = 0, i, ok;

    if (g_active && g_playing >= 0 && g_playing < g_list.count)
        recent_set(g_list.items[g_playing].path, g_pos);

    n += sprintf_lite(buf + n, sizeof(buf) - n, "[ui]\r\n");
    n += sprintf_lite(buf + n, sizeof(buf) - n, "volume=%d\r\n", g_vol);
    n += sprintf_lite(buf + n, sizeof(buf) - n, "mode=%d\r\n", g_mode);
    n += sprintf_lite(buf + n, sizeof(buf) - n, "sort=%d\r\n", g_sort);
    n += sprintf_lite(buf + n, sizeof(buf) - n, "sortdir=%d\r\n", g_sort_desc);
    n += sprintf_lite(buf + n, sizeof(buf) - n, "mintray=%d\r\n", g_min_to_tray);
    n += sprintf_lite(buf + n, sizeof(buf) - n, "mini=%d\r\n", g_mini_on_min);
    n += sprintf_lite(buf + n, sizeof(buf) - n, "closetray=%d\r\nmini_top=%d\r\nfade=%d\r\ngapless=%d\r\ntheme=%d\r\nlang=%d\r\n", g_close_to_tray, g_mini_top, g_fade_on, g_gapless_on, g_theme, g_lang);
    n += sprintf_lite(buf + n, sizeof(buf) - n, "miniw=%d\r\nminih=%d\r\n", g_mini_w, g_mini_h);
    {
        RECT wr;
        if (g_mini)
            wr = g_full_rect;
        else if (IsIconic(g_hwnd))
            wr = g_full_rect;
        else
            GetWindowRect(g_hwnd, &wr);
        n += sprintf_lite(buf + n, sizeof(buf) - n, "winx=%d\r\nwiny=%d\r\nwinw=%d\r\nwinh=%d\r\n",
                          (int)wr.left, (int)wr.top, (int)(wr.right - wr.left),
                          (int)(wr.bottom - wr.top));
    }
    if (g_sel >= 0 && g_sel < g_list.count) {
        n += sprintf_lite(buf + n, sizeof(buf) - n, "last=%s\r\n", g_list.items[g_sel].path);
        n += sprintf_lite(buf + n, sizeof(buf) - n, "pos=%d\r\n", g_pos > 0 ? g_pos : 0);
    } else {
        n += sprintf_lite(buf + n, sizeof(buf) - n, "last=\r\npos=0\r\n");
    }
    n += sprintf_lite(buf + n, sizeof(buf) - n, "device=%d\r\n", g_device);
    for (i = 0; i < RECENT_N && n < (int)sizeof(buf) - SY_PATH - 64; i++) {
        if (!g_recent_path[i][0])
            continue;
        n += sprintf_lite(buf + n, sizeof(buf) - n, "p%d=%s\r\np%dm=%d\r\n",
                          i, g_recent_path[i], i, g_recent_ms[i]);
    }
    n += sprintf_lite(buf + n, sizeof(buf) - n, "\r\n[list]\r\ncount=%d\r\n", g_list.count);
    for (i = 0; i < g_list.count && n < (int)sizeof(buf) - SY_PATH - 32; i++)
        n += sprintf_lite(buf + n, sizeof(buf) - n, "%d=%s\r\n", i, g_list.items[i].path);

    ok = write_file_all(g_ini, buf, n);
    if (!ok) {
        SetFileAttributesA(g_ini, FILE_ATTRIBUTE_NORMAL);
        ok = write_file_all(g_ini, buf, n);
    }
    meta_cache_sync(g_ini, &g_list);
    sy_log("saved", g_list.count, ok ? n : -1);
    g_dirty = 0;
}

/* Follow the palette: a light UI must not keep a black title bar. */
static void apply_title_theme(void)
{
    HMODULE dwm;
    typedef HRESULT (WINAPI *PFN_SET)(HWND, DWORD, LPCVOID, DWORD);
    PFN_SET fn;
    BOOL dark = g_light ? FALSE : TRUE;

    if (!g_hwnd)
        return;
    dwm = LoadLibraryA("dwmapi.dll");
    if (!dwm)
        return;
    fn = (PFN_SET)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (fn) {
        fn(g_hwnd, 20, &dark, sizeof(dark));
        fn(g_hwnd, 19, &dark, sizeof(dark));
    }
    FreeLibrary(dwm);
}

/* Show the outcome of a file-association action, including the Win32 error. */
static void assoc_report(HWND owner, int rc, const wchar_t *ok_text)
{
    wchar_t msg[320], num[16];
    if (!rc) {
        MessageBoxW(owner, tr(ok_text), tr(L"拾音"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (rc == 5 && assoc_integrity_low()) {
        MessageBoxW(owner, g_lang
                    ? L"The player is running at low integrity, so Windows blocks registry "
                      L"writes.\n\nClose the player and start Shiyin.exe from Explorer, "
                      L"then try again."
                    : L"当前程序以受限权限（低完整性级别）运行，系统不允许写入注册表。\n\n"
                      L"请关闭本程序，然后从资源管理器直接双击 Shiyin.exe 再试一次。",
                    tr(L"拾音"), MB_OK | MB_ICONWARNING);
        return;
    }
    w_from_int(rc, num, 16);
    if (g_lang) {
        w_copy(msg, L"Cannot write to the registry (error ", 320);
        w_cat(msg, num, 320);
        w_cat(msg, L").\n\nCheck that this account can modify HKEY_CLASSES_ROOT "
                   L"(Windows 95/98) or HKEY_CURRENT_USER\\Software\\Classes.", 320);
    } else {
        w_copy(msg, L"无法写入注册表（错误 ", 320);
        w_cat(msg, num, 320);
        w_cat(msg, L"）。\n\n请确认当前账户可以修改 HKEY_CLASSES_ROOT"
                   L"（Windows 95/98）或 HKEY_CURRENT_USER\\Software\\Classes。", 320);
    }
    MessageBoxW(owner, msg, tr(L"拾音"), MB_OK | MB_ICONWARNING);
}

static void menu_str(HMENU m, UINT flags, UINT_PTR id, const wchar_t *text)
{
    char buf[160];
    w_ansi(tr(text), buf, sizeof(buf));
    AppendMenuA(m, flags, id, buf);
}

static void context_menu(int x, int y)
{
    HMENU m = CreatePopupMenu();
    HMENU sub;
    POINT p;
    int idx;

    p.x = x;
    p.y = y;
    ClientToScreen(g_hwnd, &p);
    if (!m)
        return;
    if (row_at(y, &idx)) {
        g_sel = idx;
        invalidate_all();
        menu_str(m, MF_STRING, 1, L"播放");
        menu_str(m, MF_STRING, 2, L"从列表移除");
        menu_str(m, MF_STRING, 14, L"打开所在文件夹");
        menu_str(m, MF_STRING, 15, L"复制路径");
        menu_str(m, MF_SEPARATOR, 0, L"");
    }
    menu_str(m, MF_STRING, 3, L"添加文件...");
    menu_str(m, MF_STRING, 7, L"上移");
    menu_str(m, MF_STRING, 8, L"下移");
    menu_str(m, MF_STRING, 9, L"恢复手动顺序");
    menu_str(m, MF_SEPARATOR, 0, L"");
    menu_str(m, MF_STRING, 13, L"隐藏到托盘");
    menu_str(m, MF_STRING, 5, L"导入播放列表...");
    menu_str(m, MF_STRING, 6, L"导出播放列表...");
    menu_str(m, MF_STRING, 4, L"清空列表");
    menu_str(m, MF_SEPARATOR, 0, L"");
    {
        HMENU dv = CreatePopupMenu();
        UINT nd = waveOutGetNumDevs();
        UINT k;
        char label[96];
        w_ansi(tr(L"输出设备"), label, sizeof(label));
        menu_str(dv, MF_STRING, 20, L"系统默认");
        for (k = 0; k < nd && k < 6; k++) {
            WAVEOUTCAPSA caps;
            memset(&caps, 0, sizeof(caps));
            if (waveOutGetDevCapsA(k, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
                AppendMenuA(dv, MF_STRING, 21 + k, caps.szPname);
        }
        AppendMenuA(m, MF_POPUP, (UINT_PTR)dv, label);
    }
    {
        HMENU sl = CreatePopupMenu();
        char label[64];
        menu_str(sl, MF_STRING, 16, L"关闭定时");
        menu_str(sl, MF_STRING, 17, L"15 分钟后停止");
        menu_str(sl, MF_STRING, 18, L"30 分钟后停止");
        menu_str(sl, MF_STRING, 19, L"60 分钟后停止");
        w_ansi(tr(L"睡眠定时"), label, sizeof(label));
        AppendMenuA(m, MF_POPUP, (UINT_PTR)sl, label);
    }
    sub = CreatePopupMenu();
    if (sub) {
        char label[64];
        menu_str(sub, MF_STRING, 10, L"添加到「打开方式」");
        menu_str(sub, MF_STRING, 11, L"设为默认播放器");
        menu_str(sub, MF_STRING, 12, L"取消文件关联");
        w_ansi(tr(L"文件关联"), label, sizeof(label));
        AppendMenuA(m, MF_POPUP, (UINT_PTR)sub, label);
    }
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RIGHTBUTTON, p.x, p.y, 0, g_hwnd, NULL);
    DestroyMenu(m);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_taskbar_msg && msg == g_taskbar_msg) {
        g_tray_added = 0;
        g_tray_tries = 0;
        tray_add();
        return 0;
    }
    switch (msg) {
    case WM_CREATE: {
        char title[64], exe[SY_PATH];
        int n;
        g_hwnd = hwnd;
        {
            MEMORYSTATUS ms;
            memset(&ms, 0, sizeof(ms));
            ms.dwLength = sizeof(ms);
            GlobalMemoryStatus(&ms);
            {
                DWORD ver = GetVersion();
                char self[SY_PATH];
                GetModuleFileNameA(NULL, self, SY_PATH);
                sy_log("shiyin", 100, 0);          /* build id */
                sy_log_str("exe", self);
                sy_log("windows", (int)(ver & 0xFF), (int)((ver >> 8) & 0xFF));
                sy_log("mem_mb", (int)(ms.dwTotalPhys / (1024u * 1024u)), 0);
                g_low_integrity = assoc_integrity_low();
                sy_log("integrity_low", g_low_integrity, 0);
            }
        }
        GetModuleFileNameA(g_inst, exe, SY_PATH);
        path_dir(exe, g_ini, sizeof(g_ini));
        n = (int)strlen(g_ini);
        if (n && g_ini[n - 1] == '\\')
            g_ini[n - 1] = 0;
        strncat(g_ini, "\\shiyin.ini", sizeof(g_ini) - strlen(g_ini) - 1);
        {
            RECT rr;
            GetWindowRect(hwnd, &rr);
            g_full_rect = rr;
            g_win_x = ini_get_int(g_ini, "ui", "winx", -1);
            g_win_y = ini_get_int(g_ini, "ui", "winy", -1);
            g_win_w = ini_get_int(g_ini, "ui", "winw", 0);
            g_win_h = ini_get_int(g_ini, "ui", "winh", 0);
            sy_log("geom", g_win_w, g_win_h);
            g_geom_pending = 1;
        }
        {
            char cache[SY_PATH];
            cache[0] = 0;
            strncat(cache, g_ini, sizeof(cache) - 1);
            cache[strlen(cache) - 11] = 0;      /* strip \shiyin.ini */
            strncat(cache, "\\cache", sizeof(cache) - strlen(cache) - 1);
            art_set_cache(cache);
        }
        load_settings();
        add_command_line_files();
        rebuild_view();
        if (g_sel < 0 && g_view_n > 0)
            g_sel = g_view[0];
        sync_art();
        w_ansi(tr(L"拾音 · Shiyin"), title, sizeof(title));
        SetWindowTextA(hwnd, title);
        g_win_x = ini_get_int(g_ini, "ui", "winx", -1);
        g_win_y = ini_get_int(g_ini, "ui", "winy", -1);
        g_win_w = ini_get_int(g_ini, "ui", "winw", 0);
        g_win_h = ini_get_int(g_ini, "ui", "winh", 0);
        g_msg_wheel = RegisterWindowMessageA("MSWHEEL_ROLLMSG");
        g_taskbar_msg = RegisterWindowMessageA("TaskbarCreated");
        DragAcceptFiles(hwnd, TRUE);
        audio_init(hwnd);
        audio_set_fade(g_fade_on);
        audio_set_volume(g_vol);
        if (g_autoplay >= 0 && g_autoplay < g_list.count)
            play_track(g_autoplay);
        apply_title_theme();
        SetTimer(hwnd, ID_TIMER, 200, NULL);
        tray_add();
        return 0;
    }
    case WM_SIZE:
        g_layout_w = g_layout_h = -1;
        do_resize(hwnd);
        invalidate_all();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g_mdc && g_mbmp) {
            /* Repaint only the dirty rectangle.  The back buffer keeps the rest
             * of the previous frame, so a 200 ms progress tick costs a few
             * hundred pixels instead of the whole window (this is what made
             * playback stutter on slow Win9x machines). */
            RECT rc = ps.rcPaint;
            if (rc.right > rc.left && rc.bottom > rc.top) {
                HRGN rgn = CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);
                g_fast_bar = (g_drag == 1 && rc.top >= g_mh - S(BAR_H)) ? 1 : 0;
                if (rgn) {
                    SelectClipRgn(g_mdc, rgn);
                    paint(g_mdc, g_mw, g_mh);
                    SelectClipRgn(g_mdc, NULL);
                    DeleteObject(rgn);
                }
                BitBlt(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                       g_mdc, rc.left, rc.top, SRCCOPY);
                g_fast_bar = 0;
            }
        } else {
            RECT rc;
            GetClientRect(hwnd, &rc);
            paint(dc, rc.right, rc.bottom);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        /* the compact player must be allowed to shrink well below the full
         * window's minimum, otherwise SetWindowPos gets clamped */
        mm->ptMinTrackSize.x = g_mini ? S(240) : S(760);
        mm->ptMinTrackSize.y = g_mini ? S(84) : S(520);
        return 0;
    }
    case WM_LBUTTONDOWN:
        on_down((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_LBUTTONUP:
        if (g_drag == 1) {
            int target = g_seek_preview;
            g_drag = 0;
            g_seek_preview = -1;
            if (target >= 0)
                seek_to(target);
            return 0;
        }
        if (g_drag == 3) {
            g_drag = 0;
            g_mini_dragging = 0;
            seek_to(g_mini_drag_ms);
            return 0;
        }
        g_drag = 0;
        if (g_drag_row >= 0 || g_press_row >= 0)
            drag_commit();
        return 0;
    case WM_NCLBUTTONDBLCLK:
        if (g_mini) {
            leave_mini();
            return 0;
        }
        break;
    case WM_LBUTTONDBLCLK: {
        if (g_mini) { leave_mini(); return 0; }
        if (g_settings)
            return 0;                 /* fast clicking on a toggle is not a double click on a row */
        int idx;
        if (row_at((short)HIWORD(lp), &idx))
            play_track(idx);
        return 0;
    }
    case WM_MOUSEMOVE:
        on_move((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_RBUTTONUP:
        context_menu((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_KEYDOWN:
        on_key((int)wp);
        return 0;
    case WM_CHAR:
        on_char((unsigned int)wp);
        return 0;
    case SY_WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            tray_show_window();
            break;
        case WM_RBUTTONUP:
            tray_menu();
            break;
        default:
            break;
        }
        return 0;
    case WM_APP + 31:
        /* restore the saved window rectangle once the window is on screen */
        if (g_win_x > -10000 && g_win_y > -10000 &&
            g_win_w >= S(600) && g_win_h >= S(400))
            SetWindowPos(hwnd, NULL, g_win_x, g_win_y, g_win_w, g_win_h, SWP_NOZORDER);
        /* a resize can leave parts of the back buffer stale: force one full
         * repaint now that the final geometry is known */
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
        return 0;
    case SY_WM_TRACK: {
        char path[SY_PATH];
        int idx;
        audio_current_path(path, sizeof(path));
        idx = list_find(&g_list, path);
        if (idx >= 0) {
            int nx;
            g_playing = idx;
            g_sel = idx;
            g_pos = 0;
            g_dur = audio_duration();
            sync_art();
            sync_lyrics();
            ensure_visible(idx);
            nx = next_index_for(idx);
            audio_set_next(g_gapless_on && nx >= 0 ? g_list.items[nx].path : "");
        }
        invalidate_all();
        return 0;
    }
    case WM_COPYDATA: {
        COPYDATASTRUCT *cd = (COPYDATASTRUCT *)lp;
        if (cd && cd->dwData == 0x5348 && cd->lpData && cd->cbData > 0 && cd->cbData < 8192) {
            static char buf[8192];
            int idx;
            memcpy(buf, cd->lpData, cd->cbData);
            buf[8191] = 0;
            idx = add_files_from_text(buf);
            if (idx >= 0) {
                play_track(idx);
                ensure_visible(idx);
            }
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            g_dirty = 1;
            invalidate_all();
        }
        return 1;
    }
    case WM_DROPFILES:
        on_drop((HDROP)wp);
        return 0;
    case WM_TIMER: {
        int newpos;
        if (wp == 2) {
            if (g_anim_t < 256) {
                g_anim_t += 64;
                if (g_anim_t > 256) g_anim_t = 256;
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_drag_row < 0) {
                KillTimer(hwnd, 2);
            }
            return 0;
        }
        if (wp != ID_TIMER)
            return 0;
        if (g_search_on) {
            g_caret = !g_caret;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (g_active && !g_settings) {
            int paused = audio_paused();
            if (paused != g_paused) {
                g_paused = paused;
                if (paused && g_playing >= 0 && g_playing < g_list.count) {
                    recent_set(g_list.items[g_playing].path, g_pos);
                    g_dirty = 1;
                }
                invalidate_all();
            }
            newpos = audio_position();
            if (newpos != g_pos && g_drag != 1) {
                g_pos = newpos;
                invalidate_bar();
            }
            g_anim++;
            if (!g_paused)
                invalidate_playing_row();
        }
        if (g_theme == 2 && (int)(GetTickCount() - g_theme_check) > 60000) {
            int was = g_light;
            g_theme_check = GetTickCount();
            theme_apply();
            if (g_light != was) {
                apply_title_theme();
                invalidate_all();
            }
        }
        if (g_sleep_at && (int)(GetTickCount() - g_sleep_at) >= 0) {
            g_sleep_at = 0;
            stop_playback();
            toast(L"睡眠定时：已停止播放");
        }
        if (!g_tray_added && g_tray_tries < 3) {
            g_tray_tries++;
            tray_add();
        }
        {
            /* keep the ini fresh: list/volume changes within a second, the
             * playback position every five seconds */
            DWORD now = GetTickCount();
            if (g_active && !g_paused && now - g_pos_at > 5000) {
                g_pos_at = now;
                if (g_playing >= 0 && g_playing < g_list.count)
                    recent_set(g_list.items[g_playing].path, g_pos);
                g_dirty = 1;
            }
            if (g_dirty && now - g_save_at > 1200) {
                g_save_at = now;
                save_settings();
            }
        }
        return 0;
    }
    case SY_WM_END:
        if (g_mode == MODE_ONE) {
            if (g_playing >= 0)
                play_track(g_playing);
        } else {
            next_track(1);
        }
        return 0;
    case SY_WM_FAIL:
        switch (wp) {
        case 1: toast(L"无法解码这个文件"); break;
        case 2: toast(L"打不开声卡输出（设备被占用或格式不支持）"); break;
        case 3: toast(L"内存不足，无法播放"); break;
        case 4: toast(L"声卡没有响应，播放已停止"); break;
        default: toast(L"播放失败"); break;
        }
        sy_log("play_fail", (int)wp, 0);
        stop_playback();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case 1:
            if (g_sel >= 0) play_track(g_sel);
            break;
        case 2:
            remove_selected();
            break;
        case 3:
            open_files();
            break;
        case 20:
            g_device = -1;
            audio_set_device(-1);
            g_dirty = 1;
            toast(L"输出设备：系统默认（下次播放生效）");
            break;
        case 21: case 22: case 23: case 24: case 25: case 26: {
            int dev = LOWORD(wp) - 21;
            g_device = dev;
            audio_set_device(dev);
            g_dirty = 1;
            toast(L"已切换输出设备（下次播放生效）");
            break;
        }
        case 16:
            g_sleep_at = 0;
            toast(L"已取消睡眠定时");
            break;
        case 17:
        case 18:
        case 19: {
            int mins = (LOWORD(wp) == 17) ? 15 : (LOWORD(wp) == 18) ? 30 : 60;
            wchar_t msg[64], num[16];
            g_sleep_at = GetTickCount() + (DWORD)mins * 60000u;
            w_copy(msg, (g_lang ? L"Stopping in " : L"将在 "), 64);
            w_from_int(mins, num, 16);
            w_cat(msg, num, 64);
            w_cat(msg, (g_lang ? L" minutes" : L" 分钟后停止"), 64);
            toast(msg);
            break;
        }
        case 14:
            if (g_sel >= 0 && g_sel < g_list.count)
                reveal_in_explorer(g_list.items[g_sel].path);
            break;
        case 15:
            if (g_sel >= 0 && g_sel < g_list.count)
                copy_to_clipboard(g_list.items[g_sel].path);
            break;
        case 13:
            tray_hide();
            break;
        case 7:
            move_marked(-1);
            break;
        case 8:
            move_marked(1);
            break;
        case 9:
            g_sort = 0;
            g_sort_desc = 0;
            g_dirty = 1;
            invalidate_all();
            break;
        case 5: {
            static char fpath[SY_PATH];
            static char flt[256];
            OPENFILENAMEA ofn;
            {
                char *q = flt;
                int k;
                static const wchar_t *parts[] = {
                    L"\u64ad\u653e\u5217\u8868 (*.m3u;*.m3u8;*.pls)",
                    L"*.m3u;*.m3u8;*.pls",
                    L"\u6240\u6709\u6587\u4ef6 (*.*)", L"*.*", 0
                };
                for (k = 0; parts[k]; k++) {
                    w_ansi(parts[k], q, (int)(sizeof(flt) - (size_t)(q - flt)));
                    q += strlen(q) + 1;
                }
                *q = 0;
            }
            memset(&ofn, 0, sizeof(ofn));
            fpath[0] = 0;
            ofn.lStructSize = 76;
            ofn.hwndOwner = g_hwnd;
            ofn.lpstrFilter = flt;
            ofn.lpstrFile = fpath;
            ofn.nMaxFile = SY_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameA(&ofn)) {
                int got = playlist_import(fpath);
                if (got > 0) {
                    wchar_t msg[96], num[16];
                    w_copy(msg, (g_lang ? L"Imported " : L"已导入 "), 96);
                    w_from_int(got, num, 16);
                    w_cat(msg, num, 96);
                    w_cat(msg, g_lang ? L" tracks" : L" 首", 96);
                    toast(msg);
                } else if (got == 0) {
                    toast(L"播放列表里没有可用的文件");
                } else {
                    toast(L"无法读取播放列表");
                }
                g_dirty = 1;
            }
            break;
        }
        case 6:
            playlist_export();
            break;
        case 10:
            assoc_report(g_hwnd, assoc_add_openwith(),
                         L"已添加到「打开方式」列表。\n\n"
                         L"在音频文件上点右键 → 打开方式，应能看到「拾音」；"
                         L"若列表没有立即刷新，重启资源管理器或注销一次即可。");
            break;
        case 11:
            assoc_report(g_hwnd, assoc_set_default(),
                         L"已设为默认播放器：MP3 / FLAC / WAV / AIFF / AU。\n\n"
                         L"Windows 95/98 立即生效；Windows 10 及以后系统不允许"
                         L"程序直接改默认，只会出现在「打开方式」里。");
            break;
        case 12:
            assoc_report(g_hwnd, assoc_remove(),
                         L"已取消文件关联（只清理本程序写入的项）。");
            break;
        case 4:
            stop_playback();
            list_clear(&g_list);
            g_sel = -1;
            rebuild_view();
            invalidate_all();
            break;
        default:
            break;
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == 0xF030 /* SC_MAXIMIZE */ && g_mini) {
            return 0;
        }
        if ((wp & 0xFFF0) == 0xF020 /* SC_MINIMIZE */) {
            if (g_mini_on_min) {
                enter_mini();
                return 0;
            }
            if (g_min_to_tray && g_tray_added) {
                tray_hide();
                return 0;
            }
            if (g_min_to_tray && !g_tray_added)
                sy_log("minimize_no_tray", 0, 0);
        }
        break;
    case WM_EXITSIZEMOVE:
        if (g_mini) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (rc.right >= S(240) && rc.bottom >= S(84)) {
                g_mini_w = rc.right;
                g_mini_h = rc.bottom;
                g_dirty = 1;
            }
        }
        break;
    case WM_CLOSE:
        if (g_mini) {
            leave_mini();
            return 0;
        }
        if (g_close_to_tray && g_tray_added) {
            ShowWindow(hwnd, SW_HIDE);
            toast(L"已收进托盘，右键图标可退出");
            g_dirty = 1;
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        tray_remove();
        sy_log("exit", 0, 0);
        KillTimer(hwnd, ID_TIMER);
        save_settings();
        audio_shutdown();
        ui_done();
        PostQuitMessage(0);
        return 0;
    default:
        if (g_msg_wheel && msg == g_msg_wheel) {
            on_wheel((int)(short)HIWORD(wp));
            return 0;
        }
        if (msg == WM_MOUSEWHEEL) {
            on_wheel((int)(short)HIWORD(wp));
            return 0;
        }
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* Case insensitive "/register" style switch match on the command line. */
static int has_switch(const char *cmd, const char *sw)
{
    int n = (int)strlen(sw);
    const char *p;
    for (p = cmd; *p; p++) {
        if ((*p == '/' || *p == '-') &&
            (p == cmd || p[-1] == ' ' || p[-1] == '"' || p[-1] == '\t')) {
            int i = 0, same = 1;
            while (i < n && p[1 + i]) {
                char a = p[1 + i], b = sw[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) {
                    same = 0;
                    break;
                }
                i++;
            }
            if (same && i == n && (p[1 + n] == 0 || p[1 + n] == ' '))
                return 1;
        }
    }
    return 0;
}

static void enable_dpi_aware(void)
{
    HMODULE u = GetModuleHandleA("user32.dll");
    typedef BOOL (WINAPI *PFN)(void);
    PFN fn;
    if (!u)
        return;
    fn = (PFN)GetProcAddress(u, "SetProcessDPIAware");
    if (fn)
        fn();
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    MSG msg;
    (void)prev;
    (void)cmd;

    g_inst = inst;
    {
        /* Headless maintenance switches: register / unregister file types. */
        char *cl = GetCommandLineA();
        sy_log_str("cmdline", cl);
        sy_log("switches", has_switch(cl, "register"), has_switch(cl, "unregister"));
        if (has_switch(cl, "unregister")) {
            assoc_report(NULL, assoc_remove(), L"已取消文件关联。");
            return 0;
        }
        if (has_switch(cl, "default")) {
            assoc_report(NULL, assoc_set_default(), L"已设为默认播放器。");
            return 0;
        }
        if (has_switch(cl, "register")) {
            assoc_report(NULL, assoc_add_openwith(), L"已添加到「打开方式」。");
            return 0;
        }
    }
    {
        /* Only one player: hand the arguments to the running instance. */
        HWND prev = FindWindowA("ShiyinMain", NULL);
        if (prev) {
            char *cl = GetCommandLineA();
            char *rest;
            char path[SY_PATH];
            rest = next_cmd_arg(cl, path, sizeof(path));
            if (rest && *rest) {
                COPYDATASTRUCT cd;
                cd.dwData = 0x5348;
                cd.cbData = (DWORD)strlen(rest) + 1;
                cd.lpData = rest;
                SendMessageA(prev, WM_COPYDATA, 0, (LPARAM)&cd);
            }
            ShowWindow(prev, SW_SHOW);
            SetForegroundWindow(prev);
            return 0;
        }
    }
    enable_dpi_aware();
    ui_init();

    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hIcon = LoadIconA(inst, (LPCSTR)1);
    if (!wc.hIcon)
        wc.hIcon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
    wc.lpszClassName = "ShiyinMain";
    if (!RegisterClassA(&wc))
        return 0;

    {
        /* Create the window directly at the remembered rectangle: applying it
         * after ShowWindow produced a visible flash from the default size. */
        char ini[SY_PATH], exe[SY_PATH];
        int wx = -1, wy = -1, ww = 0, wh = 0;
        GetModuleFileNameA(NULL, exe, SY_PATH);
        path_dir(exe, ini, sizeof(ini));
        strncat(ini, "shiyin.ini", sizeof(ini) - strlen(ini) - 1);
        wx = ini_get_int(ini, "ui", "winx", -1);
        wy = ini_get_int(ini, "ui", "winy", -1);
        ww = ini_get_int(ini, "ui", "winw", 0);
        wh = ini_get_int(ini, "ui", "winh", 0);
        if (wx < -10000 || wy < -10000 || ww < S(600) || wh < S(400) ||
            wx > GetSystemMetrics(0) - 200 || wy > GetSystemMetrics(1) - 120) {
            wx = CW_USEDEFAULT;
            wy = CW_USEDEFAULT;
            ww = S(1000);
            wh = S(660);
        }
        sy_log("geom_start", (int)wx, (int)ww);
        g_hwnd = CreateWindowExA(0, "ShiyinMain", "Shiyin", WS_OVERLAPPEDWINDOW,
                                 wx, wy, ww, wh, NULL, NULL, inst, NULL);
    }
    if (!g_hwnd)
        return 0;
    ShowWindow(g_hwnd, show ? show : SW_SHOW);
    UpdateWindow(g_hwnd);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

void sy_entry(void)
{
    int r = WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), SW_SHOW);
    ExitProcess((UINT)r);
}
