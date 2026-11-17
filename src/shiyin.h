/* SPDX-License-Identifier: MPL-2.0 */
/* shiyin.h -- 拾音 / Shiyin: a minimal local music player.
 *
 * Target: Windows 95 (Win32, ANSI/GDI, no CRT, no COM, no shell extensions).
 * Everything here sticks to APIs that exist in the original Windows 95
 * kernel32/user32/gdi32/winmm/shell32/comdlg32 so the produced PE image can
 * load on a stock 95 machine.
 */
#ifndef SHIYIN_H
#define SHIYIN_H

#define WIN32_LEAN_AND_MEAN
#ifndef WINVER
#define WINVER 0x0400
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0400
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0400
#endif

#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stddef.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef signed short       s16;

/* ===================================================================== */
/*  playlist / metadata model                                            */
/* ===================================================================== */

#define SY_TEXT 192
#define SY_PATH 520

enum {
    FMT_UNKNOWN = 0,
    FMT_MP3,
    FMT_WAV,
    FMT_AIFF,
    FMT_AU,
    FMT_FLAC
};

const wchar_t *fmt_name(int fmt);
const char    *fmt_ext(int fmt);

typedef struct SyTrack {
    char    path[SY_PATH];      /* ANSI (CP_ACP) path used for all file I/O */
    wchar_t title[SY_TEXT];
    wchar_t artist[SY_TEXT];
    wchar_t album[SY_TEXT];
    wchar_t company[SY_TEXT];   /* album artist / band / composer */
    wchar_t genre[64];
    wchar_t year[16];
    int     track_no;
    int     disc_no;
    int     duration_ms;
    int     bitrate;            /* kbps, 0 when unknown */
    int     sample_rate;        /* Hz */
    int     channels;
    int     bits;
    int     format;
    unsigned int file_size;
    int     has_art;
} SyTrack;

typedef struct SyList {
    SyTrack *items;
    int      count;
    int      cap;
} SyList;

/* tags.c -- container + ID3 parsing, cover extraction */
int  tags_probe(SyTrack *t);
int  tags_cover(const char *path, u8 **data, int *len);

/* flac.c -- self contained FLAC decoder (lossless, 4..32 bit, 1..8 ch) */
typedef struct FlacDec FlacDec;

typedef struct {
    int       sample_rate;
    int       channels;
    int       bits;
    long long total_samples;
    u32       audio_off;
    int       min_blocksize;
    int       max_blocksize;
} FlacInfo;

int      flac_probe(const u8 *data, u32 size, FlacInfo *info);
int      flac_meta(const u8 *data, u32 size, const u8 **vorbis, u32 *vorbis_len,
                   const u8 **picture, u32 *picture_len);
FlacDec *flac_open_mem(const u8 *data, u32 size, FlacInfo *info);
int      flac_read_pcm(FlacDec *d, s16 *out, int frames);
int      flac_seek_sample(FlacDec *d, long long sample);
void     flac_close_mem(FlacDec *d);

/* decode.c -- audio decoding, always yields interleaved stereo s16 */
typedef struct {
    int       rate;
    int       channels;
    int       duration_ms;
    long long frames;
} SyAudio;

typedef struct SyDec SyDec;

SyDec *dec_open(const char *path, SyAudio *info);
/* frames = sample frames requested; writes frames*2 shorts. returns frames read. */
int    dec_read(SyDec *d, s16 *out, int frames);
int    dec_seek(SyDec *d, int ms);
void   dec_close(SyDec *d);
/* cheap header-only probe used while filling the playlist */
int    audio_probe(const char *path, SyTrack *t);

/* memory mapped file (read only) */
typedef struct {
    void   *base;
    u32     size;
    HANDLE  file;
    HANDLE  map;
} SyFile;

int  file_map(const char *path, SyFile *f);
void file_unmap(SyFile *f);

/* audio.c -- waveOut output engine */
#define SY_WM_POS   (WM_APP + 11)   /* wParam = position ms */
#define SY_WM_END   (WM_APP + 12)   /* decoder reached end of stream */
#define SY_WM_FAIL  (WM_APP + 13)   /* playback failed */
#define SY_WM_TRAY  (WM_APP + 21)   /* tray icon callback message */
#define SY_WM_TRACK (WM_APP + 22)   /* gapless switch: the worker moved on */

void audio_init(HWND notify);
int  audio_play(const char *path, int start_ms);
void audio_stop(void);
void audio_pause(void);
void audio_resume(void);
void audio_toggle_pause(void);
void audio_seek(int ms);
void audio_set_volume(int vol);
void audio_set_device(int idx);
void audio_set_replaygain(int mult);
void audio_set_fade(int on);
void audio_set_next(const char *path);
void audio_current_path(char *out, int cap);   /* 0..100 */
int  audio_active(void);          /* engine owns an open track */
int  audio_paused(void);
int  audio_position(void);        /* ms */
int  audio_duration(void);        /* ms */
void audio_shutdown(void);

/* image.c -- cover art decode + scale */
typedef struct {
    HBITMAP bmp;
    int     w;
    int     h;
} SyArt;

int  art_from_memory(const u8 *data, int len, int box, SyArt *out);
int  art_from_file(const char *path, int box, SyArt *out);
void art_set_cache(const char *dir);
int  art_from_memory_cached(const char *key, const u8 *data, int len, int box, SyArt *out);
int  art_from_file_cached(const char *key, const char *path, int box, SyArt *out);
void art_free(SyArt *a);

/* util.c -- freestanding runtime (no CRT) */
void  *sy_alloc(u32 n);
void  *sy_calloc(u32 n, u32 size);
void  *sy_realloc(void *p, u32 n);
void   sy_free(void *p);

void   sy_log(const char *tag, int a, int b);
/* assoc.c -- file type registration */
int    assoc_add_openwith(void);
int    assoc_set_default(void);
int    assoc_remove(void);
int    assoc_is_default(void);
int    assoc_integrity_low(void);
#define ASSOC_NEEDS_UI 1        /* registered; the system UI was opened */
void   sy_log_str(const char *tag, const char *text);
int    w_len(const wchar_t *s);
void   w_copy(wchar_t *dst, const wchar_t *src, int cap);
void   w_cat(wchar_t *dst, const wchar_t *src, int cap);
void   w_trim(wchar_t *s);
int    w_ieq_ascii(const wchar_t *a, const wchar_t *b);   /* ascii case-insensitive */
int    w_contains(const wchar_t *hay, const wchar_t *needle); /* ascii-insensitive */
int    w_from_ansi(const char *src, wchar_t *dst, int cap);
int    w_from_utf8(const char *src, int bytes, wchar_t *dst, int cap);
int    w_from_utf16(const u8 *src, int bytes, int big_endian, wchar_t *dst, int cap);
int    w_from_latin1(const char *src, int bytes, wchar_t *dst, int cap);
void   w_from_int(int v, wchar_t *buf, int cap);
void   w_ansi(const wchar_t *src, char *dst, int cap);

/* file helpers */
int    file_exists(const char *path);
int    file_size_of(const char *path);
void   path_dir(const char *path, char *dir, int cap);
void   path_base(const char *path, wchar_t *out, int cap);   /* file name */
void   path_join(char *out, int cap, const char *dir, const char *name);
int    ext_is(const char *path, const char *ext);
int    is_dir(const char *path);

/* tracking allocator guard: memory is grown with sy_realloc */
int    list_add(SyList *l, const char *path);
int    list_add_tree(SyList *l, const char *path, int depth);
void   list_clear(SyList *l);
int    list_find(const SyList *l, const char *path);
int    list_load(SyList *l, const char *ini);
/* metadata cache: lets a restart skip tag parsing for unchanged files */
void   meta_cache_load(const char *file);
int    meta_cache_hit(const char *path, SyTrack *t);
void   meta_cache_save(const char *file, const SyList *l);
void   meta_cache_sync(const char *ini, const SyList *l);
void   list_save(const SyList *l, const char *ini);

void   ini_get_str(const char *ini, const char *sec, const char *key,
                   const char *def, char *out, int cap);
int    ini_get_int(const char *ini, const char *sec, const char *key, int def);
void   ini_set_int(const char *ini, const char *sec, const char *key, int v);
int    sprintf_lite(char *out, int cap, const char *fmt, ...);
int    write_file_all(const char *path, const void *data, int len);
void   ini_set_str(const char *ini, const char *sec, const char *key, const char *v);
int    path_has_ext(const char *path, const char *ext);

/* lrc.c -- .lrc lyrics */
int            lrc_load(const char *audio_path);
void           lrc_clear(void);
int            lrc_count(void);
const wchar_t *lrc_text(int i);
int            lrc_ms(int i);
int            lrc_index_at(int ms);

/* ui.c -- GDI drawing helpers + theme */
extern int g_dpi;

int      S(int px);
HFONT    ui_font(int px, int weight);
void     ui_init(void);
void     ui_done(void);

void     ui_fill(HDC dc, int x, int y, int w, int h, COLORREF c);
void     ui_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c);
void     ui_round(HDC dc, int x, int y, int w, int h, int r, COLORREF fill, COLORREF border);
void     ui_gradient(HDC dc, int x, int y, int w, int h, COLORREF top, COLORREF bottom);
void     ui_text(HDC dc, const wchar_t *s, int len, RECT *r, COLORREF color,
                 HFONT f, UINT flags);
void     ui_text_at(HDC dc, const wchar_t *s, int len, int x, int y, COLORREF color, HFONT f);
int      ui_text_w(HDC dc, const wchar_t *s, int len, HFONT f);
void     ui_glyph(HDC dc, int glyph, int x, int y, int size, COLORREF color);
void     ui_eq(HDC dc, int x, int y, int size, COLORREF color, int phase);

enum {
    GLYPH_PLAY = 1, GLYPH_PAUSE, GLYPH_PREV, GLYPH_NEXT, GLYPH_SHUFFLE,
    GLYPH_REPEAT, GLYPH_REPEAT1, GLYPH_VOL, GLYPH_VOL_MUTE, GLYPH_SEARCH,
    GLYPH_PLUS, GLYPH_FOLDER, GLYPH_NOTE, GLYPH_TRASH, GLYPH_MUSIC,
    GLYPH_ORDER
};

/* theme */
extern COLORREF C_BG;
extern COLORREF C_PANEL;
extern COLORREF C_CARD;
extern COLORREF C_LINE;
extern COLORREF C_LINE_SOFT;
extern COLORREF C_TEXT;
extern COLORREF C_TEXT2;
extern COLORREF C_TEXT3;
extern COLORREF C_ACCENT;
extern COLORREF C_ACCENT_D;
extern COLORREF C_HOVER;
extern COLORREF C_SELECT;

/* interface language: 0 = Chinese, 1 = English */
extern int g_lang;
const wchar_t *tr(const wchar_t *s);

#endif /* SHIYIN_H */
