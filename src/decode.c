/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* decode.c -- audio decoding to interleaved stereo s16.
 *
 * Containers/codecs kept self-contained so a stock Windows 95 (which ships no
 * MP3 codec) still plays audio:
 *   MP3   MPEG-1/2/2.5 Layer III  (minimp3, integer only)
 *   WAV   PCM 8/16/24/32, float32, mu-law, A-law, IMA ADPCM
 *   AIFF  PCM (big endian / sowt), mu-law, A-law
 *   AU    PCM 8/16/24/32, float, mu-law, A-law
 */
#include "shiyin.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "../third_party/minimp3.h"

/* ------------------------------------------------------------------ */
/*  byte helpers                                                       */
/* ------------------------------------------------------------------ */

static u16 rd16le(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32le(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static u16 rd16be(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static u32 rd32be(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}
static u32 fourcc(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

/* ------------------------------------------------------------------ */
/*  decoder object                                                     */
/* ------------------------------------------------------------------ */

enum { K_PCM = 0, K_MP3 = 1, K_FLAC = 2 };



typedef struct {
    u32 off;
    u32 samples;
} Mp3Frame;

struct SyDec {
    int      kind;
    SyFile   f;
    const u8 *data;
    u32      size;

    int      rate;
    int      channels;      /* source channel count */
    int      bits;
    int      codec;         /* wav/aiff/au codec id */
    int      big_endian;
    int      block_align;

    u32      data_off;
    u32      data_len;
    u32      pos;           /* current byte position inside data */

    /* IMA ADPCM */
    int      ima_index[2];
    int      ima_pred[2];
    s16      ima_buf[2 * 4096];
    int      ima_buf_pos;
    int      ima_buf_count;

    /* MP3 */
    mp3dec_t mp3;
    Mp3Frame *frames;
    int       frame_count;
    int       frame_cap;
    int       frame_idx;
    int       pending;      /* samples still to hand out from current frame */
    long long total_samples;
    int       nominal_bitrate;
    s16       pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    /* FLAC */
    FlacDec  *flac;
};

/* ------------------------------------------------------------------ */
/*  codec conversion                                                   */
/* ------------------------------------------------------------------ */

static s16 mulaw_decode(u8 u)
{
    int t;
    u ^= 0xFF;
    t = ((u & 0x0F) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return (s16)((u & 0x80) ? (0x84 - t) : (t - 0x84));
}

static s16 alaw_decode(u8 a)
{
    int t, seg;
    a ^= 0x55;
    t = (a & 0x0F) << 4;
    seg = (a & 0x70) >> 4;
    if (seg == 0)
        t += 8;
    else if (seg == 1)
        t += 0x108;
    else {
        t += 0x108;
        t <<= seg - 1;
    }
    return (s16)((a & 0x80) ? t : -t);
}

static s16 clamp16(int v)
{
    if (v > 32767)
        return 32767;
    if (v < -32768)
        return -32768;
    return (s16)v;
}

/* Convert one block of `frames` sample frames to stereo s16. */
static void convert_pcm(SyDec *d, const u8 *src, int frames, s16 *out)
{
    int i, ch;
    int nch = d->channels;
    int bps = d->bits / 8;

    for (i = 0; i < frames; i++) {
        s16 l = 0, r = 0;
        for (ch = 0; ch < nch; ch++) {
            const u8 *p = src + (i * nch + ch) * bps;
            int v = 0;
            switch (d->codec) {
            case 1: /* PCM int */
                if (bps == 1)
                    v = ((int)p[0] - 128) << 8;
                else if (bps == 2)
                    v = d->big_endian ? (int)(short)rd16be(p) : (int)(short)rd16le(p);
                else if (bps == 3) {
                    int t = d->big_endian ? (int)(((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2])
                                          : (int)(((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0]);
                    if (t & 0x800000)
                        t -= 0x1000000;
                    v = t >> 8;
                } else if (bps == 4) {
                    int t = d->big_endian ? (int)rd32be(p) : (int)rd32le(p);
                    v = t >> 16;
                }
                break;
            case 3: { /* IEEE float32 */
                union { u32 u; float f; } cv;
                cv.u = d->big_endian ? rd32be(p) : rd32le(p);
                v = clamp16((int)(cv.f * 32767.0f));
                break;
            }
            case 6: /* mu-law */
                v = (int)mulaw_decode(p[0]) << 2;
                break;
            case 7: /* A-law */
                v = (int)alaw_decode(p[0]) << 2;
                break;
            default:
                v = 0;
                break;
            }
            if (ch == 0)
                l = clamp16(v);
            if (ch == 1)
                r = clamp16(v);
        }
        if (nch == 1)
            r = l;
        out[i * 2] = l;
        out[i * 2 + 1] = r;
    }
}

static const int ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767
};
static const int ima_index[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};

static int ima_nibble(int *pred, int *index, int nib)
{
    int step = ima_step[*index];
    int diff = step >> 3;
    if (nib & 1) diff += step >> 2;
    if (nib & 2) diff += step >> 1;
    if (nib & 4) diff += step;
    if (nib & 8) diff = -diff;
    *pred += diff;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *index += ima_index[nib];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;
    return *pred;
}

/* Decode one IMA ADPCM block into d->ima_buf. */
static int ima_decode_block(SyDec *d)
{
    const u8 *blk = d->data + d->pos;
    u32 remain = d->data_len + d->data_off - d->pos;
    int nch = d->channels;
    int block = d->block_align;
    int spb, n = 0, ch;
    u32 p = 0;

    if (block <= 4 * nch || remain < (u32)block)
        return 0;
    spb = (block - 4 * nch) * 2 / nch + 1;
    if (spb > 4096)
        spb = 4096;

    for (ch = 0; ch < nch; ch++) {
        d->ima_pred[ch] = (s16)rd16le(blk + p);
        d->ima_index[ch] = blk[p + 2];
        if (d->ima_index[ch] > 88)
            d->ima_index[ch] = 88;
        p += 4;
    }
    d->ima_buf[0] = (s16)d->ima_pred[0];
    d->ima_buf[1] = (s16)(nch > 1 ? d->ima_pred[1] : d->ima_pred[0]);
    n = 1;

    if (nch == 1) {
        int half = 0;
        u8 byte = 0;
        while (n < spb && p < (u32)block) {
            int nib;
            if (!half) {
                byte = blk[p++];
                nib = byte & 0x0F;
                half = 1;
            } else {
                nib = (byte >> 4) & 0x0F;
                half = 0;
            }
            d->ima_buf[n * 2] = d->ima_buf[n * 2 + 1] =
                (s16)ima_nibble(&d->ima_pred[0], &d->ima_index[0], nib);
            n++;
        }
    } else {
        /* stereo: groups of 4 bytes per channel hold 8 nibbles each */
        while (n < spb && p + 8 <= (u32)block) {
            int k;
            for (k = 0; k < 8 && n + k < spb; k++) {
                u8 byte = blk[p + (k >> 1)];
                int nib = (k & 1) ? ((byte >> 4) & 0x0F) : (byte & 0x0F);
                s16 v = (s16)ima_nibble(&d->ima_pred[0], &d->ima_index[0], nib);
                d->ima_buf[(n + k) * 2] = v;
                d->ima_buf[(n + k) * 2 + 1] = v;
            }
            for (k = 0; k < 8 && n + k < spb; k++) {
                u8 byte = blk[p + 4 + (k >> 1)];
                int nib = (k & 1) ? ((byte >> 4) & 0x0F) : (byte & 0x0F);
                d->ima_buf[(n + k) * 2 + 1] =
                    (s16)ima_nibble(&d->ima_pred[1], &d->ima_index[1], nib);
            }
            n += 8;
            p += 8;
        }
        if (n > spb)
            n = spb;
    }
    d->pos += (u32)block;
    d->ima_buf_pos = 0;
    d->ima_buf_count = n;
    return n;
}

/* ------------------------------------------------------------------ */
/*  container parsing                                                  */
/* ------------------------------------------------------------------ */

static int parse_wav(SyDec *d)
{
    const u8 *p = d->data;
    u32 off = 12;
    int got_fmt = 0;

    if (d->size < 12 || fourcc(p) != fourcc((const u8 *)"RIFF") ||
        fourcc(p + 8) != fourcc((const u8 *)"WAVE"))
        return 0;
    while (off + 8 <= d->size) {
        u32 id = fourcc(p + off);
        u32 len = rd32le(p + off + 4);
        u32 body = off + 8;
        if (body + len > d->size)
            len = d->size - body;
        if (id == fourcc((const u8 *)"fmt ")) {
            int tag = rd16le(p + body);
            d->channels = rd16le(p + body + 2);
            d->rate = (int)rd32le(p + body + 4);
            d->block_align = rd16le(p + body + 12);
            d->bits = rd16le(p + body + 14);
            if (tag == 0xFFFE && len >= 40) {
                tag = rd16le(p + body + 24);
                d->bits = rd16le(p + body + 18);
            }
            d->codec = tag;
            if (tag == 1 && d->bits == 0)
                d->bits = 16;
            if (tag == 0x11) { /* IMA ADPCM */
                d->bits = 4;
                if (d->block_align <= 0)
                    d->block_align = 256;
            }
            if (tag == 6 || tag == 7)
                d->bits = 8;
            if (tag == 3 && d->bits != 32)
                return 0;              /* only 32 bit float is supported */
            got_fmt = 1;
        } else if (id == fourcc((const u8 *)"data")) {
            d->data_off = body;
            d->data_len = len;
            if (got_fmt)
                break;
        }
        off = body + len + (len & 1);
    }
    if (!got_fmt || !d->data_off)
        return 0;
    d->big_endian = 0;
    d->kind = K_PCM;
    return 1;
}

/* 80-bit IEEE extended -> Hz */
static int aiff_rate(const u8 *p)
{
    int exp = (int)(((p[0] & 0x7F) << 8) | p[1]);
    u32 mant = rd32be(p + 2);
    double r;
    int e;
    if (exp == 0 && mant == 0)
        return 0;
    /* value = mant * 2^(exp - 16383 - 31) */
    r = (double)mant;
    e = exp - 16383 - 31;
    while (e > 0) { r *= 2.0; e--; }
    while (e < 0) { r *= 0.5; e++; }
    return (int)(r + 0.5);
}

static int parse_aiff(SyDec *d)
{
    const u8 *p = d->data;
    u32 off = 12;
    int got_comm = 0, got_ssnd = 0;
    u32 ssnd_body = 0;

    if (d->size < 12 || fourcc(p) != fourcc((const u8 *)"FORM"))
        return 0;
    if (fourcc(p + 8) != fourcc((const u8 *)"AIFF") && fourcc(p + 8) != fourcc((const u8 *)"AIFC"))
        return 0;
    while (off + 8 <= d->size) {
        u32 id = fourcc(p + off);
        u32 len = rd32be(p + off + 4);
        u32 body = off + 8;
        if (body + len > d->size)
            len = d->size - body;
        if (id == fourcc((const u8 *)"COMM")) {
            d->channels = rd16be(p + body);
            d->rate = aiff_rate(p + body + 8);
            d->bits = rd16be(p + body + 6);
            d->codec = 1;
            d->block_align = (d->bits / 8) * d->channels;
            if (fourcc(p + 8) == fourcc((const u8 *)"AIFC") && len >= 22) {
                u32 comp = fourcc(p + body + 18);
                if (comp == fourcc((const u8 *)"sowt")) {
                    d->big_endian = 0;
                } else if (comp == fourcc((const u8 *)"ulaw") || comp == fourcc((const u8 *)"ULAW")) {
                    d->codec = 6;
                    d->bits = 8;
                } else if (comp == fourcc((const u8 *)"alaw") || comp == fourcc((const u8 *)"ALAW")) {
                    d->codec = 7;
                    d->bits = 8;
                } else {
                    d->big_endian = 1;
                }
            } else {
                d->big_endian = 1;
            }
            got_comm = 1;
        } else if (id == fourcc((const u8 *)"SSND")) {
            u32 offset = rd32be(p + body);
            ssnd_body = body + 8 + offset;
            got_ssnd = 1;
        }
        off = body + len + (len & 1);
    }
    if (!got_comm || !got_ssnd)
        return 0;
    d->data_off = ssnd_body;
    d->data_len = d->size - ssnd_body;
    d->kind = K_PCM;
    if (!d->block_align)
        d->block_align = (d->bits / 8) * d->channels;
    return 1;
}

static int parse_au(SyDec *d)
{
    const u8 *p = d->data;
    u32 off, len, enc, rate, ch;
    if (d->size < 24 || fourcc(p) != fourcc((const u8 *)".snd"))
        return 0;
    off = rd32be(p + 4);
    len = rd32be(p + 8);
    enc = rd32be(p + 12);
    rate = rd32be(p + 16);
    ch = rd32be(p + 20);
    if (ch < 1 || ch > 8 || rate < 1000 || off >= d->size)
        return 0;
    d->channels = (int)ch;
    d->rate = (int)rate;
    d->big_endian = 1;
    switch (enc) {
    case 1: d->codec = 6; d->bits = 8; break;
    case 2: d->codec = 1; d->bits = 8; break;
    case 3: d->codec = 1; d->bits = 16; break;
    case 4: d->codec = 1; d->bits = 24; break;
    case 5: d->codec = 1; d->bits = 32; break;
    case 6: d->codec = 3; d->bits = 32; break;
    case 27: d->codec = 7; d->bits = 8; break;
    default: return 0;
    }
    if (d->codec == 6 || d->codec == 7) {
        d->big_endian = 0;
        d->bits = 8;
    }
    d->data_off = off;
    d->data_len = (len && off + len <= d->size) ? len : d->size - off;
    d->block_align = (d->bits / 8) * d->channels;
    d->kind = K_PCM;
    return 1;
}

/* ---------------- MP3 ---------------- */

static const int mp3_bitrate_v1[16] = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
};
static const int mp3_bitrate_v2[16] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
};
static const int mp3_srate[4][3] = {
    { 44100, 48000, 32000 },   /* MPEG1 */
    { 22050, 24000, 16000 },   /* MPEG2 */
    { 11025, 12000, 8000 },    /* MPEG2.5 */
    { 0, 0, 0 }
};

typedef struct {
    int version;   /* 0=2.5, 1=2, 2=1 */
    int layer;     /* 3=III */
    int rate;
    int channels;
    int bitrate;
    int frame_bytes;
    int samples;
} Mp3Header;

static int mp3_parse_header(const u8 *p, int avail, Mp3Header *h)
{
    u32 v;
    int bri, sri, pad, ver, layer;
    if (avail < 4)
        return 0;
    v = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
    if ((v & 0xFFE00000u) != 0xFFE00000u)
        return 0;
    ver = (int)((v >> 19) & 3);
    layer = (int)((v >> 17) & 3);
    if (ver == 1 || layer != 1)     /* reserved version / not Layer III */
        return 0;
    bri = (int)((v >> 12) & 0x0F);
    sri = (int)((v >> 10) & 3);
    if (bri == 0 || bri == 15 || sri == 3)
        return 0;
    pad = (int)((v >> 9) & 1);
    h->version = ver == 3 ? 2 : (ver == 2 ? 1 : 0);
    h->layer = 3;
    h->rate = mp3_srate[2 - h->version][sri];
    h->channels = ((v >> 6) & 3) == 3 ? 1 : 2;
    h->bitrate = (h->version == 2 ? mp3_bitrate_v1 : mp3_bitrate_v2)[bri];
    h->samples = h->version == 2 ? 1152 : 576;
    h->frame_bytes = (h->version == 2)
        ? (144 * h->bitrate * 1000 / h->rate) + pad
        : (72 * h->bitrate * 1000 / h->rate) + pad;
    return h->frame_bytes > 4 ? 1 : 0;
}

static u32 id3v2_size(const u8 *p, u32 size)
{
    if (size >= 10 && p[0] == 'I' && p[1] == 'D' && p[2] == '3' &&
        p[3] != 0xFF && p[4] != 0xFF) {
        u32 sz = ((u32)(p[6] & 0x7F) << 21) | ((u32)(p[7] & 0x7F) << 14) |
                 ((u32)(p[8] & 0x7F) << 7) | (u32)(p[9] & 0x7F);
        u32 total = 10 + sz + ((p[5] & 0x10) ? 10 : 0);
        return total < size ? total : size;
    }
    return 0;
}

static int parse_mp3(SyDec *d, int build_index)
{
    u32 off = id3v2_size(d->data, d->size);
    u32 first = 0;
    int frames = 0, rate = 0, ch = 2, br = 0;
    long long samples = 0;
    Mp3Header h;

    /* find the first sync that is confirmed by a second frame */
    while (off + 4 < d->size) {
        if (mp3_parse_header(d->data + off, (int)(d->size - off), &h)) {
            if (off + (u32)h.frame_bytes + 4 < d->size) {
                Mp3Header h2;
                if (mp3_parse_header(d->data + off + h.frame_bytes,
                                     (int)(d->size - off - h.frame_bytes), &h2)) {
                    first = off;
                    rate = h.rate;
                    ch = h.channels;
                    br = h.bitrate;
                    break;
                }
            } else {
                first = off;
                rate = h.rate;
                ch = h.channels;
                br = h.bitrate;
                break;
            }
        }
        off++;
    }
    if (!first && !rate)
        return 0;

    if (build_index) {
        d->frame_cap = 1024;
        d->frames = (Mp3Frame *)sy_alloc((u32)d->frame_cap * sizeof(Mp3Frame));
        if (!d->frames)
            return 0;
    }
    off = first;
    while (off + 4 < d->size) {
        if (!mp3_parse_header(d->data + off, (int)(d->size - off), &h))
            break;
        if (off + (u32)h.frame_bytes > d->size)
            break;
        if (build_index) {
            if (d->frame_count >= d->frame_cap) {
                int nc = d->frame_cap * 2;
                Mp3Frame *nf = (Mp3Frame *)sy_realloc(d->frames, (u32)nc * sizeof(Mp3Frame));
                if (!nf)
                    break;
                d->frames = nf;
                d->frame_cap = nc;
            }
            d->frames[d->frame_count].off = off;
            d->frames[d->frame_count].samples = (u32)samples;
            d->frame_count++;
        }
        samples += h.samples;
        frames++;
        off += (u32)h.frame_bytes;
    }
    if (!frames)
        return 0;

    d->kind = K_MP3;
    d->rate = rate;
    d->channels = ch;
    d->bits = 16;
    d->codec = 1;
    d->data_off = first;
    d->data_len = d->size - first;
    d->block_align = 4;
    d->pending = 0;
    d->frame_idx = 0;
    d->pos = first;
    d->total_samples = samples;
    d->nominal_bitrate = br;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  public decoder API                                                 */
/* ------------------------------------------------------------------ */

/* FLAC: metadata only first, decoder opened afterwards. */
static int parse_flac_dec(SyDec *d, int open_decoder)
{
    FlacInfo fi;
    if (!flac_probe(d->data, d->size, &fi))
        return 0;
    d->kind = K_FLAC;
    d->rate = fi.sample_rate;
    d->channels = fi.channels;
    d->bits = fi.bits;
    d->total_samples = fi.total_samples;
    d->data_off = fi.audio_off;
    d->pos = fi.audio_off;
    if (open_decoder) {
        d->flac = flac_open_mem(d->data, d->size, &fi);
        if (!d->flac)
            return 0;
    }
    return 1;
}

SyDec *dec_open(const char *path, SyAudio *info)
{
    SyDec *d = (SyDec *)sy_calloc(1, sizeof(SyDec));
    if (!d)
        return 0;
    if (!file_map(path, &d->f)) {
        sy_free(d);
        return 0;
    }
    d->data = (const u8 *)d->f.base;
    d->size = d->f.size;
    d->pos = 0;

    if (!parse_wav(d) && !parse_aiff(d) && !parse_au(d) && !parse_flac_dec(d, 1) &&
        !parse_mp3(d, 1)) {
        file_unmap(&d->f);
        sy_free(d);
        return 0;
    }
    if (d->kind == K_MP3) {
        mp3dec_init(&d->mp3);
        d->pos = d->data_off;
    } else {
        d->pos = d->data_off;
        if (!d->block_align)
            d->block_align = (d->bits / 8) * d->channels;
    }
    if (info) {
        info->rate = d->rate;
        info->channels = d->channels;
        if (d->kind == K_MP3 || d->kind == K_FLAC) {
            info->frames = d->total_samples;
            info->duration_ms = (int)(d->total_samples * 1000 / (d->rate ? d->rate : 44100));
        } else if (d->codec == 0x11) {
            int nch = d->channels ? d->channels : 1;
            int spb = (d->block_align - 4 * nch) * 2 / nch + 1;
            long long blocks = (long long)d->data_len / (d->block_align ? d->block_align : 1);
            info->frames = blocks * (spb > 0 ? spb : 1);
            info->duration_ms = (int)(info->frames * 1000 / (d->rate ? d->rate : 44100));
        } else {
            int bpf = d->block_align ? d->block_align : 4;
            info->frames = (long long)(d->data_len / (u32)bpf);
            info->duration_ms = (int)(info->frames * 1000 / (d->rate ? d->rate : 44100));
        }
    }
    return d;
}

int dec_read(SyDec *d, s16 *out, int frames)
{
    int done = 0;
    if (!d || frames <= 0)
        return 0;

    if (d->kind == K_FLAC) {
        if (!d->flac)
            return 0;
        return flac_read_pcm(d->flac, out, frames);
    }

    if (d->kind == K_MP3) {
        while (done < frames) {
            int i;
            if (d->pending <= 0) {
                mp3dec_frame_info_t fi;
                int samples;
                if (d->pos + 4 > d->size)
                    break;
                samples = mp3dec_decode_frame(&d->mp3, d->data + d->pos,
                                              (int)(d->size - d->pos), d->pcm, &fi);
                if (!fi.frame_bytes)
                    break;
                d->pos += (u32)fi.frame_bytes;
                if (samples <= 0) {
                    continue;
                }
                d->pending = samples;
                d->frame_idx = 0;
                if (fi.channels == 1) {
                    /* expand mono in place, from the back */
                    for (i = samples - 1; i >= 0; i--) {
                        d->pcm[i * 2] = d->pcm[i];
                        d->pcm[i * 2 + 1] = d->pcm[i];
                    }
                }
            }
            {
                int n = d->pending;
                if (n > frames - done)
                    n = frames - done;
                memcpy(out + done * 2, d->pcm + d->frame_idx * 2, (size_t)n * 4);
                d->frame_idx += n;
                d->pending -= n;
                done += n;
            }
        }
        return done;
    }

    /* PCM family */
    if (d->codec == 0x11) {   /* IMA ADPCM */
        while (done < frames) {
            int n;
            if (d->ima_buf_pos >= d->ima_buf_count) {
                if (d->pos + (u32)d->block_align > d->data_off + d->data_len)
                    break;
                if (ima_decode_block(d) <= 0)
                    break;
            }
            n = d->ima_buf_count - d->ima_buf_pos;
            if (n > frames - done)
                n = frames - done;
            memcpy(out + done * 2, d->ima_buf + d->ima_buf_pos * 2, (size_t)n * 4);
            d->ima_buf_pos += n;
            done += n;
        }
        return done;
    }
    {
        int bps = d->bits / 8;
        int bpf = bps * d->channels;
        int avail;
        if (!bpf || !d->rate)
            return 0;
        avail = (int)((d->data_off + d->data_len - d->pos) / (u32)bpf);
        if (avail > frames - done)
            avail = frames - done;
        if (avail <= 0)
            return 0;
        convert_pcm(d, d->data + d->pos, avail, out + done * 2);
        d->pos += (u32)avail * (u32)bpf;
        done += avail;
    }
    return done;
}

int dec_seek(SyDec *d, int ms)
{
    if (!d || ms < 0)
        return 0;
    if (d->kind == K_FLAC) {
        if (!d->flac)
            return 0;
        return flac_seek_sample(d->flac, (long long)ms * d->rate / 1000);
    }
    if (d->kind == K_MP3) {
        long long target = (long long)ms * d->rate / 1000;
        int lo = 0, hi = d->frame_count - 1, best = 0;
        if (d->frame_count <= 0)
            return 0;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if ((long long)d->frames[mid].samples <= target) {
                best = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        mp3dec_init(&d->mp3);
        d->pos = d->frames[best].off;
        d->pending = 0;
        {
            long long skip = target - (long long)d->frames[best].samples;
            while (skip > 0) {
                mp3dec_frame_info_t fi;
                int samples = mp3dec_decode_frame(&d->mp3, d->data + d->pos,
                                                  (int)(d->size - d->pos), d->pcm, &fi);
                if (!fi.frame_bytes)
                    break;
                d->pos += (u32)fi.frame_bytes;
                if (samples <= 0)
                    continue;
                if (skip >= samples) {
                    skip -= samples;
                } else {
                    if (fi.channels == 1) {
                        int i;
                        for (i = samples - 1; i >= 0; i--) {
                            d->pcm[i * 2] = d->pcm[i];
                            d->pcm[i * 2 + 1] = d->pcm[i];
                        }
                    }
                    memmove(d->pcm, d->pcm + (int)skip * 2,
                            (size_t)(samples - (int)skip) * 4);
                    d->pending = samples - (int)skip;
                    d->frame_idx = 0;
                    skip = 0;
                }
            }
        }
        return 1;
    }
    if (d->codec == 0x11) {
        int nch = d->channels ? d->channels : 1;
        int spb = (d->block_align - 4 * nch) * 2 / nch + 1;
        long long want = (long long)ms * d->rate / 1000;
        long long blocks = want / (spb > 0 ? spb : 1);
        u32 pos = d->data_off + (u32)blocks * (u32)(d->block_align ? d->block_align : 1);
        if (pos > d->data_off + d->data_len)
            pos = d->data_off + d->data_len;
        d->pos = pos;
        d->ima_buf_pos = 0;
        d->ima_buf_count = 0;
        return 1;
    }
    {
        int bpf = (d->bits / 8) * d->channels;
        long long frame = (long long)ms * d->rate / 1000;
        u32 pos;
        if (!bpf)
            return 0;
        pos = d->data_off + (u32)(frame * bpf);
        if (pos > d->data_off + d->data_len)
            pos = d->data_off + d->data_len;
        d->pos = pos;
        return 1;
    }
}

void dec_close(SyDec *d)
{
    if (!d)
        return;
    if (d->flac)
        flac_close_mem(d->flac);
    if (d->frames)
        sy_free(d->frames);
    file_unmap(&d->f);
    sy_free(d);
}

/* ------------------------------------------------------------------ */
/*  probe: fill playlist metadata without decoding                     */
/* ------------------------------------------------------------------ */

int audio_probe(const char *path, SyTrack *t)
{
    SyDec tmp;
    SyAudio info;

    memset(&tmp, 0, sizeof(tmp));
    if (!file_map(path, &tmp.f))
        return 0;
    tmp.data = (const u8 *)tmp.f.base;
    tmp.size = tmp.f.size;

    if (parse_wav(&tmp)) {
        t->format = FMT_WAV;
    } else if (parse_aiff(&tmp)) {
        t->format = FMT_AIFF;
    } else if (parse_au(&tmp)) {
        t->format = FMT_AU;
    } else if (parse_flac_dec(&tmp, 0)) {
        t->format = FMT_FLAC;
    } else if (parse_mp3(&tmp, 0)) {
        t->format = FMT_MP3;
    } else {
        file_unmap(&tmp.f);
        return 0;
    }
    t->sample_rate = tmp.rate;
    t->channels = tmp.channels;
    t->bits = tmp.bits;
    if (tmp.kind == K_MP3) {
        info.frames = tmp.total_samples;
        info.duration_ms = (int)(info.frames * 1000 / (tmp.rate ? tmp.rate : 44100));
        t->bitrate = tmp.nominal_bitrate;
        if (t->bitrate <= 0 && info.duration_ms > 0) {
            long long bits = (long long)t->file_size * 8;
            t->bitrate = (int)(bits / info.duration_ms);
        }
    } else if (tmp.kind == K_FLAC) {
        info.frames = tmp.total_samples;
        info.duration_ms = (int)(info.frames * 1000 / (tmp.rate ? tmp.rate : 44100));
        if (info.duration_ms > 0) {
            long long bits = (long long)t->file_size * 8;
            t->bitrate = (int)(bits / info.duration_ms);
        }
    } else {
        int bpf = tmp.block_align ? tmp.block_align : 4;
        long long frames = (long long)(tmp.data_len / (u32)bpf);
        info.frames = frames;
        info.duration_ms = (int)(frames * 1000 / (tmp.rate ? tmp.rate : 44100));
        if (info.duration_ms > 0 && t->file_size > 0) {
            long long bps = (long long)tmp.rate * tmp.channels * (tmp.bits ? tmp.bits : 8);
            t->bitrate = (int)(bps / 1000);
        }
    }
    t->duration_ms = info.duration_ms;
    file_unmap(&tmp.f);
    return 1;
}

