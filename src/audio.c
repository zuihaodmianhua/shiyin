/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* audio.c -- waveOut playback engine.
 *
 * One worker thread owns the whole output path (device open, buffer ring,
 * seeking, volume).  The UI only flips command flags and reads back position.
 *
 * Windows 95 notes:
 *  - CALLBACK_EVENT is used instead of CALLBACK_FUNCTION: the Win9x MMSYSTEM
 *    callback runs in a 16 bit thunk context and has bitten more than one
 *    player into a deadlock; an event plus WHDR_DONE polling is dull but safe.
 *  - The worker runs at NORMAL priority; boosting it on a single core Win9x
 *    machine starves the UI thread and looks exactly like a frozen window.
 *  - A watchdog fails the track if the device stops making progress, so a
 *    wedged driver can never leave the player stuck "playing" forever.
 */
#include "shiyin.h"

#define NBUF 4
#define PROGRESS_TIMEOUT 6000

typedef struct {
    HWND     hwnd;
    HANDLE   thread;
    HANDLE   evt;
    volatile int quit;

    volatile int want_play;
    volatile int want_stop;
    volatile int want_seek;
    volatile int want_pause;
    volatile int want_resume;
    volatile int play_start;
    volatile int seek_ms;

    volatile int active;
    volatile int paused;
    volatile int eof;
    volatile int finished;
    volatile int pos_ms;
    volatile int frames_done;

    int      vol;
    int      muted;
    int      gain;
    int      fade;          /* 0..65536, 250 ms fade in */
    int      fade_inc;
    int      device;        /* -1 = WAVE_MAPPER */
    int      rg;
    int      fade_on;
    int      fade_stop;    /* ramping down before pausing */
    DWORD    pause_at;            /* ReplayGain multiplier, 256 = 1.0 */

    char     path[SY_PATH];
    char     next_path[SY_PATH];
    char     playing_path[SY_PATH];
    int      next_used;
    SyDec   *dec;
    HWAVEOUT out;
    WAVEHDR  hdr[NBUF];
    s16     *buf[NBUF];
    int      busy[NBUF];
    int      busy_frames[NBUF];
    int      buf_frames;
    int      rate;
    int      src_rate;
    int      duration_ms;
    int      base_ms;
    DWORD    last_progress;
    int      reported_fail;

    int      resample;
    s16     *tmp;
    int      tmp_cap;
    int      tmp_pos;
    int      tmp_count;
    int      frac;
} Engine;

static Engine g;

/* ------------------------------------------------------------------ */
/*  device lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void device_close(void)
{
    int i;

    if (!g.out)
        return;
    waveOutReset(g.out);
    Sleep(10);
    for (i = 0; i < NBUF; i++) {
        waveOutUnprepareHeader(g.out, &g.hdr[i], sizeof(WAVEHDR));
        if (g.buf[i]) {
            sy_free(g.buf[i]);
            g.buf[i] = 0;
        }
        memset(&g.hdr[i], 0, sizeof(WAVEHDR));
        g.busy[i] = 0;
        g.busy_frames[i] = 0;
    }
    waveOutClose(g.out);
    g.out = 0;
}

static void engine_close(void)
{
    int i, guard;
    if (g.out) {
        waveOutReset(g.out);
        Sleep(30);                     /* reset returns every queued block */
        (void)guard;
        for (i = 0; i < NBUF; i++)
            g.busy[i] = 0;
        for (i = 0; i < NBUF; i++) {
            waveOutUnprepareHeader(g.out, &g.hdr[i], sizeof(WAVEHDR));
            if (g.buf[i]) {
                sy_free(g.buf[i]);
                g.buf[i] = 0;
            }
            memset(&g.hdr[i], 0, sizeof(WAVEHDR));
            g.busy[i] = 0;
            g.busy_frames[i] = 0;
        }
        waveOutClose(g.out);
        g.out = 0;
    }
    if (g.dec) {
        dec_close(g.dec);
        g.dec = 0;
    }
    g.next_used = 0;
    if (g.tmp) {
        sy_free(g.tmp);
        g.tmp = 0;
        g.tmp_cap = 0;
    }
    g.active = 0;
    g.paused = 0;
    g.eof = 0;
    g.finished = 0;
}

static int engine_open_out(int rate)
{
    WAVEFORMATEX wf;
    MMRESULT r;
    int i;

    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = (DWORD)rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = (DWORD)rate * 4;
    wf.cbSize = 0;

    if (g.out && g.rate == rate) {
        /* same format: keep the device, just drop whatever is queued */
        int i;
        waveOutReset(g.out);
        for (i = 0; i < NBUF; i++)
            g.busy[i] = 0;
        return 1;
    }
    if (g.out)
        device_close();

    r = waveOutOpen(&g.out, (UINT)(g.device >= 0 ? g.device : WAVE_MAPPER), &wf,
                    (DWORD_PTR)g.evt, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR && g.device >= 0) {
        sy_log("device_fail", (int)r, g.device);
        g.device = -1;
    g.fade_on = 1;
    g.rg = 256;
        r = waveOutOpen(&g.out, WAVE_MAPPER, &wf, (DWORD_PTR)g.evt, 0, CALLBACK_EVENT);
    }
    if (r != MMSYSERR_NOERROR) {
        sy_log("waveOutOpen", (int)r, rate);
        g.out = 0;
        return 0;
    }
    g.rate = rate;
    /* ~100 ms per block, four blocks: on a single core Win9x box a 250 ms
     * cushion was not enough to ride out a repaint, 400 ms is. */
    g.buf_frames = rate / 10;
    if (g.buf_frames < 1024)
        g.buf_frames = 1024;
    if (g.buf_frames > 8192)
        g.buf_frames = 8192;

    for (i = 0; i < NBUF; i++) {
        g.buf[i] = (s16 *)sy_alloc((u32)g.buf_frames * 4);
        if (!g.buf[i]) {
            engine_close();
            return 0;
        }
        memset(g.buf[i], 0, (u32)g.buf_frames * 4);
        memset(&g.hdr[i], 0, sizeof(WAVEHDR));
        g.hdr[i].lpData = (LPSTR)g.buf[i];
        g.hdr[i].dwBufferLength = (DWORD)g.buf_frames * 4;
        g.hdr[i].dwUser = 0;
        g.busy[i] = 0;
        g.busy_frames[i] = 0;
        if (waveOutPrepareHeader(g.out, &g.hdr[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            engine_close();
            return 0;
        }
    }
    sy_log("waveOutOpen", 0, rate);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  decoding into the output buffers                                   */
/* ------------------------------------------------------------------ */

static void apply_gain(s16 *p, int frames)
{
    int target = g.muted ? 0 : g.vol * 256 / 100;
    int i, n = frames * 2;
    for (i = 0; i < n; i++) {
        if (g.gain < target) {
            g.gain += 6;
            if (g.gain > target)
                g.gain = target;
        } else if (g.gain > target) {
            g.gain -= 6;
            if (g.gain < target)
                g.gain = target;
        }
        if (!g.fade_on) {
            g.fade = 65536;
        } else if (g.fade_stop) {
            if (g.fade > 0) {
                g.fade -= g.fade_inc;
                if (g.fade < 0)
                    g.fade = 0;
            }
        } else if (g.fade < 65536) {
            g.fade += g.fade_inc;
            if (g.fade > 65536)
                g.fade = 65536;
        }
        {
            long long v = ((long long)p[i] * g.gain) >> 8;
            v = (v * (long long)(g.fade >> 8)) >> 8;
            v = (v * g.rg) >> 8;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            p[i] = (s16)v;
        }
    }
}

static int read_pcm(s16 *dst, int want)
{
    int n;
    if (!g.resample) {
        n = dec_read(g.dec, dst, want);
        if (n <= 0) {
            g.eof = 1;
            return 0;
        }
        apply_gain(dst, n);
        return n;
    }
    {
        int out = 0;
        while (out < want) {
            if (g.tmp_pos >= g.tmp_count) {
                int r = dec_read(g.dec, g.tmp, g.tmp_cap);
                if (r <= 0) {
                    g.eof = 1;
                    break;
                }
                g.tmp_pos = 0;
                g.tmp_count = r;
            }
            {
                /* linear interpolation between the current and next source
                 * frame: much cleaner than dropping samples when the file
                 * rate does not match the device rate */
                int l0 = g.tmp[g.tmp_pos * 2];
                int r0 = g.tmp[g.tmp_pos * 2 + 1];
                int l1 = l0, r1 = r0;
                int w;
                if (g.tmp_pos + 1 < g.tmp_count) {
                    l1 = g.tmp[(g.tmp_pos + 1) * 2];
                    r1 = g.tmp[(g.tmp_pos + 1) * 2 + 1];
                }
                w = (int)(((long long)g.frac << 16) / (g.rate ? g.rate : 1));
                if (w < 0) w = 0;
                if (w > 65535) w = 65535;
                dst[out * 2] = (s16)(l0 + (int)(((long long)(l1 - l0) * w) >> 16));
                dst[out * 2 + 1] = (s16)(r0 + (int)(((long long)(r1 - r0) * w) >> 16));
            }
            out++;
            g.frac += g.src_rate;
            while (g.frac >= g.rate) {
                g.frac -= g.rate;
                g.tmp_pos++;
                if (g.tmp_pos >= g.tmp_count)
                    break;
            }
        }
        apply_gain(dst, out);
        return out;
    }
}

/* Reap finished buffers: the driver sets WHDR_DONE when a block was played. */
static void reap(void)
{
    int i;
    for (i = 0; i < NBUF; i++) {
        if (g.busy[i] && (g.hdr[i].dwFlags & WHDR_DONE)) {
            g.busy[i] = 0;
            g.frames_done += g.busy_frames[i];
            g.last_progress = GetTickCount();
        }
    }
}

static int fill_one(int idx)
{
    int n;
    WAVEHDR *h = &g.hdr[idx];

    if (g.busy[idx])
        return 0;
    if (g.eof)
        return 0;
    n = read_pcm(g.buf[idx], g.buf_frames);
    if (n <= 0 && g.next_path[0] && !g.next_used) {
        /* seamless: continue with the next track inside the same buffer */
        SyAudio info;
        char next[SY_PATH];
        strcpy(next, g.next_path);
        g.next_path[0] = 0;
        g.next_used = 1;
        dec_close(g.dec);
        g.dec = 0;
        memset(&info, 0, sizeof(info));
        g.dec = dec_open(next, &info);
        if (g.dec && info.rate == g.rate) {
            g.base_ms = 0;
            g.frames_done = 0;
            g.eof = 0;
            g.finished = 0;
            g.duration_ms = info.duration_ms;
            strcpy(g.playing_path, next);
            g.tmp_pos = g.tmp_count = 0;
            g.frac = 0;
            sy_log("gapless", 1, info.duration_ms);
            PostMessage(g.hwnd, SY_WM_TRACK, 0, 0);
            n = read_pcm(g.buf[idx], g.buf_frames);
        } else {
            if (g.dec) {
                dec_close(g.dec);
                g.dec = 0;
            }
            sy_log("gapless", 0, 0);
        }
    }
    if (n <= 0)
        return 0;
    if (n < g.buf_frames)
        memset(g.buf[idx] + n * 2, 0, (size_t)(g.buf_frames - n) * 4);
    h->dwFlags &= ~(DWORD)WHDR_DONE;    /* keep WHDR_PREPARED */
    h->dwUser = (DWORD_PTR)n;
    h->dwBufferLength = (DWORD)g.buf_frames * 4;
    if (waveOutWrite(g.out, h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        sy_log("waveOutWrite", idx, 0);
        g.eof = 1;
        return 0;
    }
    g.busy[idx] = 1;
    g.busy_frames[idx] = n;
    return 1;
}

static void fill_all(void)
{
    int i, guard = 0;
    while (guard++ < 8) {
        int wrote = 0;
        for (i = 0; i < NBUF; i++) {
            if (!g.busy[i] && fill_one(i)) {
                wrote = 1;
                break;
            }
        }
        if (!wrote)
            break;
    }
}

static void update_position(void)
{
    int played = g.frames_done;
    if (played < 0)
        played = 0;
    g.pos_ms = g.base_ms + (int)((long long)played * 1000 / (g.rate ? g.rate : 44100));
}

static DWORD WINAPI worker(LPVOID param)
{
    (void)param;
    for (;;) {
        if (g.quit)
            break;

        if (g.want_stop) {
            g.want_stop = 0;
            engine_close();
            g.base_ms = 0;
            g.pos_ms = 0;
            continue;
        }

        if (g.want_play) {
            SyAudio info;
            g.want_play = 0;
            if (g.out) {
                /* keep the output device, only swap the decoder */
                if (g.dec) {
                    dec_close(g.dec);
                    g.dec = 0;
                }
                if (g.tmp) {
                    sy_free(g.tmp);
                    g.tmp = 0;
                    g.tmp_cap = 0;
                }
            } else {
                engine_close();
            }
            memset(&info, 0, sizeof(info));
            g.dec = dec_open(g.path, &info);
            sy_log("dec_open", g.dec ? 1 : 0, info.duration_ms);
            if (!g.dec) {
                PostMessage(g.hwnd, SY_WM_FAIL, 1, 0);
                continue;
            }
            g.src_rate = info.rate > 0 ? info.rate : 44100;
            g.duration_ms = info.duration_ms;
            g.base_ms = g.play_start;
            g.pos_ms = g.play_start;
            g.gain = 0;
            g.resample = 0;
            g.reported_fail = 0;
            if (!engine_open_out(g.src_rate)) {
                g.resample = 1;
                if (!engine_open_out(44100)) {
                    dec_close(g.dec);
                    g.dec = 0;
                    PostMessage(g.hwnd, SY_WM_FAIL, 2, 0);
                    continue;
                }
            }
            if (g.resample) {
                g.tmp_cap = g.buf_frames * (g.src_rate / 8000 + 2) + 64;
                if (g.tmp_cap < 4096)
                    g.tmp_cap = 4096;
                g.tmp = (s16 *)sy_alloc((u32)g.tmp_cap * 4);
                if (!g.tmp) {
                    engine_close();
                    PostMessage(g.hwnd, SY_WM_FAIL, 3, 0);
                    continue;
                }
                g.tmp_pos = g.tmp_count = 0;
                g.frac = 0;
            }
            g.frames_done = 0;
            g.eof = 0;
            g.finished = 0;
            g.paused = 0;
            g.active = 1;
            g.last_progress = GetTickCount();
            g.fade = 0;
            g.fade_inc = 65536 / (g.rate > 0 ? (g.rate / 4) : 11025);
            strcpy(g.playing_path, g.path);
            g.next_used = 0;
            if (g.play_start > 0)
                dec_seek(g.dec, g.play_start);
            fill_all();
            sy_log("started", g.busy[0] + g.busy[1] + g.busy[2] + g.busy[3], g.buf_frames);
            PostMessage(g.hwnd, SY_WM_POS, 0, 0);
            continue;
        }

        if (g.want_seek && g.dec) {
            int ms = g.seek_ms;
            g.want_seek = 0;
            waveOutReset(g.out);
            {
                int i;
                for (i = 0; i < NBUF; i++)
                    g.busy[i] = 0;
            }
            g.frames_done = 0;
            g.eof = 0;
            g.finished = 0;
            g.base_ms = ms;
            g.pos_ms = ms;
            g.last_progress = GetTickCount();
            dec_seek(g.dec, ms);
            g.tmp_pos = g.tmp_count = 0;
            g.frac = 0;
            g.fade = 0;
            fill_all();
            if (g.paused)
                waveOutPause(g.out);
            continue;
        }

        if (g.want_pause && g.out) {
            g.want_pause = 0;
            if (g.fade_on && !g.fade_stop) {
                g.fade_stop = 1;
                g.pause_at = GetTickCount() + 120;
                continue;
            }
            g.fade_stop = 0;
            waveOutPause(g.out);
            g.paused = 1;
            g.last_progress = GetTickCount();
            continue;
        }
        if (g.fade_stop) {
            if ((int)(GetTickCount() - g.pause_at) >= 0) {
                g.fade_stop = 0;
                g.fade = 0;
                waveOutPause(g.out);
                g.paused = 1;
                g.last_progress = GetTickCount();
                continue;
            }
        }
        if (g.want_resume && g.out) {
            g.want_resume = 0;
            g.fade_stop = 0;
            waveOutRestart(g.out);
            g.paused = 0;
            g.last_progress = GetTickCount();
            continue;
        }

        reap();
        if (g.dec && g.out && !g.paused && !g.eof) {
            fill_all();
            update_position();
            if (!g.reported_fail && g.last_progress &&
                GetTickCount() - g.last_progress > PROGRESS_TIMEOUT) {
                g.reported_fail = 1;
                sy_log("stalled", g.frames_done, g.busy[0] + g.busy[1] + g.busy[2] + g.busy[3]);
                PostMessage(g.hwnd, SY_WM_FAIL, 4, 0);
            }
        }
        if (g.dec && g.eof && !g.busy[0] && !g.busy[1] && !g.busy[2] && !g.finished) {
            g.finished = 1;
            update_position();
            PostMessage(g.hwnd, SY_WM_END, 0, 0);
        }
        WaitForSingleObject(g.evt, g.paused ? 60 : 20);
    }
    engine_close();
    sy_log("worker_exit", 0, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  public API                                                         */
/* ------------------------------------------------------------------ */

void audio_init(HWND notify)
{
    DWORD tid = 0;

    memset(&g, 0, sizeof(g));
    g.hwnd = notify;
    g.vol = 70;
    g.device = -1;
    g.fade_on = 1;
    g.rg = 256;
    g.evt = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g.evt) {
        sy_log("CreateEvent", (int)GetLastError(), 0);
        return;
    }
    /* Windows 95 rejects a NULL lpThreadId (NT happily accepts it), and a
     * plain 2 MB reserve can fail on a small machine, so try twice. */
    g.thread = CreateThread(NULL, 0, worker, NULL, 0, &tid);
    if (!g.thread) {
        sy_log("CreateThread_err", (int)GetLastError(), 0);
        g.thread = CreateThread(NULL, 256 * 1024, worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, &tid);
    }
    sy_log("audio_init", g.thread ? 1 : 0, (int)tid);
}

int audio_play(const char *path, int start_ms)
{
    if (!g.thread)
        return 0;
    strncpy(g.path, path, SY_PATH - 1);
    g.path[SY_PATH - 1] = 0;
    g.play_start = start_ms > 0 ? start_ms : 0;
    g.want_seek = 0;
    g.want_stop = 0;
    g.want_play = 1;
    return 1;
}

void audio_stop(void)
{
    if (g.thread) {
        g.want_stop = 1;
        g.want_play = 0;
        g.want_seek = 0;
    }
}

void audio_pause(void)
{
    if (g.active)
        g.want_pause = 1;
}

void audio_resume(void)
{
    if (g.active)
        g.want_resume = 1;
}

void audio_toggle_pause(void)
{
    if (!g.active)
        return;
    if (g.paused)
        g.want_resume = 1;
    else
        g.want_pause = 1;
}

void audio_seek(int ms)
{
    if (ms < 0)
        ms = 0;
    g.seek_ms = ms;
    g.pos_ms = ms;
    g.want_seek = 1;
}

void audio_set_next(const char *path)
{
    g.next_path[0] = 0;
    if (path && path[0])
        strncpy(g.next_path, path, SY_PATH - 1);
    g.next_path[SY_PATH - 1] = 0;
}

void audio_current_path(char *out, int cap)
{
    if (!out || cap <= 0)
        return;
    strncpy(out, g.playing_path, (size_t)(cap - 1));
    out[cap - 1] = 0;
}

void audio_set_fade(int on)
{
    g.fade_on = on ? 1 : 0;
    if (!g.fade_on)
        g.fade = 65536;
}

void audio_set_replaygain(int mult)
{
    if (mult < 8) mult = 8;
    if (mult > 1024) mult = 1024;
    g.rg = mult;
}

void audio_set_device(int idx)
{
    g.device = idx;
    g.want_stop = 1;          /* reopen on next play */
}

void audio_set_volume(int vol)
{
    if (vol < 0)
        vol = 0;
    if (vol > 100)
        vol = 100;
    g.vol = vol;
}

int audio_active(void) { return g.active; }
int audio_paused(void) { return g.paused; }
int audio_position(void) { return g.pos_ms; }
int audio_duration(void) { return g.duration_ms; }

void audio_shutdown(void)
{
    if (!g.thread)
        return;
    g.quit = 1;
    if (g.evt)
        SetEvent(g.evt);
    {
        DWORD t0 = GetTickCount();
        DWORD wr = WaitForSingleObject(g.thread, 3000);
        sy_log("shutdown", (int)wr, (int)(GetTickCount() - t0));
    }
    CloseHandle(g.thread);
    g.thread = 0;
    if (g.evt) {
        CloseHandle(g.evt);
        g.evt = 0;
    }
}
