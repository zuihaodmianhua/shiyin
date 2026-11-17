/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* flac.c -- self contained FLAC decoder.
 *
 * Supports the complete FLAC bitstream as produced by the reference encoder:
 *   - STREAMINFO / VORBIS_COMMENT / PICTURE / SEEKTABLE metadata
 *   - fixed and variable block size streams
 *   - CONSTANT / VERBATIM / FIXED(0..4) / LPC(1..32) subframes
 *   - Rice coded residuals with 4 and 5 bit parameters, escape codes
 *   - wasted bits, independent / left-side / right-side / mid-side stereo
 *   - 1..8 channels, 4..32 bits per sample
 *
 * Output is always interleaved 16 bit stereo, matching the waveOut engine.
 * A lightweight frame index is built on demand for seeking.
 */
#include "shiyin.h"
#include <stdint.h>

#define FLAC_MAX_CH  8
#define FLAC_MAX_BLK 65535

/* ------------------------------------------------------------------ */
/*  bit reader (MSB first)                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const u8 *p;
    u32 size;      /* bytes available */
    u32 bit;       /* current bit position */
    int err;
} Br;

static void br_init(Br *b, const u8 *p, u32 size)
{
    b->p = p;
    b->size = size;
    b->bit = 0;
    b->err = 0;
}

static void br_skip(Br *b, u32 bits)
{
    b->bit += bits;
    if (b->bit > b->size * 8) {
        b->bit = b->size * 8;
        b->err = 1;
    }
}

static u32 br_read(Br *b, int n)
{
    u32 v = 0;
    while (n > 0) {
        u32 byte = b->bit >> 3;
        int off = (int)(b->bit & 7);
        int avail = 8 - off;
        int take = n < avail ? n : avail;
        u32 bits;
        if (byte >= b->size) {
            b->err = 1;
            return v << n;
        }
        bits = ((u32)b->p[byte] << off) & 0xFF;
        bits >>= (8 - take);
        v = (v << take) | bits;
        b->bit += (u32)take;
        n -= take;
    }
    return v;
}

static int br_signed(Br *b, int n)
{
    u32 v;
    if (n <= 0)
        return 0;
    v = br_read(b, n);
    if (n < 32 && (v & (1u << (n - 1))))
        return (int)(v | (~0u << n));
    return (int)v;
}

static u32 br_unary(Br *b)
{
    u32 count = 0;
    while (b->bit < b->size * 8) {
        u32 byte = b->bit >> 3;
        int off = (int)(b->bit & 7);
        u32 v = ((u32)b->p[byte] << off) & 0xFF;
        if (v) {
            int zeros = 0;
            while (!(v & 0x80)) {
                v <<= 1;
                zeros++;
            }
            b->bit += (u32)zeros + 1;
            return count + (u32)zeros;
        }
        b->bit += (u32)(8 - off);
        count += (u32)(8 - off);
    }
    b->err = 1;
    return count;
}

static int br_rice(Br *b, int param)
{
    u32 q = br_unary(b);
    u32 r = param ? br_read(b, param) : 0;
    u32 u = (q << param) | r;
    return (int)((u >> 1) ^ (u32)-(int)(u & 1));
}

static int br_utf8(Br *b, unsigned long long *out)
{
    u32 b0 = br_read(b, 8);
    int extra;
    unsigned long long v;

    if (!(b0 & 0x80)) {
        *out = b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0) { extra = 1; v = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; v = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; v = b0 & 0x07; }
    else if ((b0 & 0xFC) == 0xF8) { extra = 4; v = b0 & 0x03; }
    else if ((b0 & 0xFE) == 0xFC) { extra = 5; v = b0 & 0x01; }
    else if (b0 == 0xFE) { extra = 6; v = 0; }
    else {
        b->err = 1;
        return 0;
    }
    while (extra-- > 0) {
        u32 c = br_read(b, 8);
        if ((c & 0xC0) != 0x80)
            b->err = 1;
        v = (v << 6) | (c & 0x3F);
    }
    *out = v;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  CRC                                                                */
/* ------------------------------------------------------------------ */

static u8 crc8(const u8 *p, u32 n)
{
    u8 crc = 0;
    u32 i;
    int j;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (j = 0; j < 8; j++)
            crc = (u8)((crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1));
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/*  metadata                                                           */
/* ------------------------------------------------------------------ */

static void flac_read_streaminfo(const u8 *p, FlacInfo *info)
{
    info->min_blocksize = (p[0] << 8) | p[1];
    info->max_blocksize = (p[2] << 8) | p[3];
    info->sample_rate = ((int)p[10] << 12) | ((int)p[11] << 4) | (p[12] >> 4);
    info->channels = ((p[12] >> 1) & 7) + 1;
    info->bits = (((p[12] & 1) << 4) | (p[13] >> 4)) + 1;
    info->total_samples = ((long long)(p[13] & 0x0F) << 32) | ((long long)p[14] << 24) |
                          ((long long)p[15] << 16) | ((long long)p[16] << 8) | p[17];
}

/* Walk the metadata blocks.  Returns 1 when STREAMINFO was found.
 * vc/pic receive pointers into the mapped file (may be 0). */
static int flac_parse(const u8 *data, u32 size, FlacInfo *info,
                      const u8 **vc, u32 *vclen, const u8 **pic, u32 *piclen)
{
    u32 off = 4;
    int last = 0;

    memset(info, 0, sizeof(*info));
    if (vc) { *vc = 0; *vclen = 0; }
    if (pic) { *pic = 0; *piclen = 0; }
    if (size < 8 || data[0] != 'f' || data[1] != 'L' || data[2] != 'a' || data[3] != 'C')
        return 0;
    while (!last && off + 4 <= size) {
        int type = data[off] & 0x7F;
        u32 len;
        u32 body;

        last = (data[off] & 0x80) != 0;
        len = ((u32)data[off + 1] << 16) | ((u32)data[off + 2] << 8) | data[off + 3];
        body = off + 4;
        if (body + len > size)
            return 0;
        if (type == 0 && len >= 34) {
            flac_read_streaminfo(data + body, info);
        } else if (type == 4 && vc && !*vc) {
            *vc = data + body;
            *vclen = len;
        } else if (type == 6 && pic && !*pic) {
            *pic = data + body;
            *piclen = len;
        }
        off = body + len;
    }
    info->audio_off = off;
    return info->sample_rate > 0 && info->channels > 0;
}

/* ------------------------------------------------------------------ */
/*  frame header                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    int blocksize;
    int sample_rate;
    int channels;
    int bps;
    int assignment;
    int variable;
    unsigned long long number;
    u32 header_bytes;
    int final_sample;   /* block size of the last frame in a fixed stream */
} FrameHdr;

static const int flac_bs_tab[16] = {
    0, 192, 576, 1152, 2304, 4608, 0, 0, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};
static const int flac_sr_tab[12] = {
    0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000
};

static int flac_frame_header(const u8 *d, u32 size, u32 off, const FlacInfo *si, FrameHdr *h)
{
    u32 p = off;
    int bs_code, sr_code, bps_code;
    u32 crc_at;

    memset(h, 0, sizeof(*h));
    if (off + 6 > size)
        return 0;
    if (d[p] != 0xFF || (d[p + 1] & 0xFE) != 0xF8)
        return 0;
    h->variable = d[p + 1] & 1;
    bs_code = d[p + 2] >> 4;
    sr_code = d[p + 2] & 0x0F;
    h->assignment = d[p + 3] >> 4;
    bps_code = (d[p + 3] >> 1) & 7;
    if (d[p + 3] & 1)
        return 0;
    if (bs_code == 0 || sr_code == 15 || bps_code == 3)
        return 0;

    h->channels = h->assignment <= 7 ? h->assignment + 1 : 2;
    if (h->channels > FLAC_MAX_CH)
        return 0;

    p += 4;
    {
        Br br;
        unsigned long long num;
        br_init(&br, d + p, size - p);
        if (!br_utf8(&br, &num) || br.err)
            return 0;
        h->number = num;
        p += (br.bit + 7) / 8;
    }
    if (bs_code == 6) {
        if (p + 1 > size) return 0;
        h->blocksize = d[p] + 1;
        p += 1;
    } else if (bs_code == 7) {
        if (p + 2 > size) return 0;
        h->blocksize = ((d[p] << 8) | d[p + 1]) + 1;
        p += 2;
    } else {
        h->blocksize = flac_bs_tab[bs_code];
    }
    if (sr_code == 12) {
        if (p + 1 > size) return 0;
        h->sample_rate = d[p] * 1000;
        p += 1;
    } else if (sr_code == 13) {
        if (p + 2 > size) return 0;
        h->sample_rate = (d[p] << 8) | d[p + 1];
        p += 2;
    } else if (sr_code == 14) {
        if (p + 2 > size) return 0;
        h->sample_rate = ((d[p] << 8) | d[p + 1]) * 10;
        p += 2;
    } else {
        h->sample_rate = flac_sr_tab[sr_code];
    }
    if (!h->sample_rate)
        h->sample_rate = si->sample_rate;
    if (bps_code == 0)
        h->bps = si->bits;
    else if (bps_code == 1) h->bps = 8;
    else if (bps_code == 2) h->bps = 12;
    else if (bps_code == 4) h->bps = 16;
    else if (bps_code == 5) h->bps = 20;
    else if (bps_code == 6) h->bps = 24;
    else if (bps_code == 7) h->bps = 32;
    else return 0;

    crc_at = p;
    if (crc_at >= size)
        return 0;
    if (crc8(d + off, crc_at - off) != d[crc_at])
        return 0;
    h->header_bytes = crc_at - off + 1;
    if (h->bps <= 0 || h->bps > 32 || h->blocksize <= 0 || h->blocksize > FLAC_MAX_BLK)
        return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  decoder                                                            */
/* ------------------------------------------------------------------ */

struct FlacDec {
    const u8 *data;
    u32 size;
    FlacInfo si;

    u32 pos;                 /* byte offset of the next frame */
    long long cur_sample;    /* sample index of the next frame */
    int err;
    int eof;

    int32_t *chan[FLAC_MAX_CH];
    s16 *pcm;                /* one decoded frame, interleaved stereo */
    int pcm_count;
    int pcm_pos;
    long long skip;          /* frames to drop after a seek */

    u32 *idx_off;
    long long *idx_smp;
    int idx_n;
    int idx_cap;
    int indexed;
};

static int flac_decode_subframe(Br *br, int blocksize, int bps, int32_t *out);

static int flac_decode_residual(Br *br, int blocksize, int order, int32_t *out)
{
    int method = (int)br_read(br, 2);
    int porder = (int)br_read(br, 4);
    int partitions, param_bits, escape, p, i;

    if (method > 1 || br->err)
        return 0;
    partitions = 1 << porder;
    param_bits = method == 0 ? 4 : 5;
    escape = method == 0 ? 15 : 31;
    i = order;
    for (p = 0; p < partitions; p++) {
        int n = (blocksize >> porder) - (p == 0 ? order : 0);
        int param;
        if (n < 0)
            return 0;
        param = (int)br_read(br, param_bits);
        if (br->err)
            return 0;
        if (param == escape) {
            int raw = (int)br_read(br, 5);
            if (raw == 0) {
                while (n-- > 0)
                    out[i++] = 0;
            } else {
                while (n-- > 0)
                    out[i++] = br_signed(br, raw);
            }
        } else {
            while (n-- > 0)
                out[i++] = br_rice(br, param);
        }
        if (br->err)
            return 0;
    }
    return i >= blocksize;
}

static int flac_decode_subframe(Br *br, int blocksize, int bps, int32_t *out)
{
    int type, wasted = 0, i;

    if (br_read(br, 1) != 0)
        return 0;
    type = (int)br_read(br, 6);
    if (br_read(br, 1)) {
        wasted = (int)br_unary(br) + 1;
        bps -= wasted;
    }
    if (bps <= 0 || bps > 32 || br->err)
        return 0;

    if (type == 0) {                     /* constant */
        int32_t v = br_signed(br, bps);
        for (i = 0; i < blocksize; i++)
            out[i] = v;
    } else if (type == 1) {              /* verbatim */
        for (i = 0; i < blocksize; i++)
            out[i] = br_signed(br, bps);
    } else if (type >= 8 && type <= 12) {   /* fixed predictor */
        int order = type - 8;
        for (i = 0; i < order; i++)
            out[i] = br_signed(br, bps);
        if (!flac_decode_residual(br, blocksize, order, out))
            return 0;
        for (i = order; i < blocksize; i++) {
            switch (order) {
            case 0: break;
            case 1: out[i] += out[i - 1]; break;
            case 2: out[i] += 2 * out[i - 1] - out[i - 2]; break;
            case 3: out[i] += 3 * out[i - 1] - 3 * out[i - 2] + out[i - 3]; break;
            default:
                out[i] += 4 * out[i - 1] - 6 * out[i - 2] + 4 * out[i - 3] - out[i - 4];
                break;
            }
        }
    } else if (type >= 32) {             /* LPC */
        int order = type - 32 + 1;
        int prec, shift, j;
        int32_t coef[32];

        if (order > 32)
            return 0;
        for (i = 0; i < order; i++)
            out[i] = br_signed(br, bps);
        prec = (int)br_read(br, 4) + 1;
        if (prec == 16)
            return 0;
        shift = br_signed(br, 5);
        for (i = 0; i < order; i++)
            coef[i] = br_signed(br, prec);
        if (br->err)
            return 0;
        if (!flac_decode_residual(br, blocksize, order, out))
            return 0;
        for (i = order; i < blocksize; i++) {
            long long sum = 0;
            for (j = 0; j < order; j++)
                sum += (long long)coef[j] * out[i - 1 - j];
            if (shift >= 0)
                sum >>= shift;
            else
                sum <<= -shift;
            out[i] += (int32_t)sum;
        }
    } else {
        return 0;                        /* reserved subframe type */
    }
    if (wasted) {
        for (i = 0; i < blocksize; i++)
            out[i] = (int32_t)((u32)out[i] << wasted);
    }
    return !br->err;
}

static s16 flac_to_s16(int32_t v, int bps)
{
    if (bps > 16) {
        int sh = bps - 16;
        long long x = ((long long)v + (1 << (sh - 1))) >> sh;
        if (x > 32767)
            x = 32767;
        if (x < -32768)
            x = -32768;
        return (s16)x;
    }
    if (bps < 16) {
        long long x = (long long)v << (16 - bps);
        if (x > 32767)
            x = 32767;
        if (x < -32768)
            x = -32768;
        return (s16)x;
    }
    return (s16)v;
}

/* Decode the frame at d->pos.  Returns 1 on success. */
static int flac_decode_next(FlacDec *d)
{
    FrameHdr h;
    Br br;
    u32 start = d->pos;
    int i, ch;
    int bps;

    if (d->pos + 6 > d->size) {
        d->eof = 1;
        return 0;
    }
    if (!flac_frame_header(d->data, d->size, d->pos, &d->si, &h)) {
        /* resync: scan forward for the next plausible frame header */
        u32 p = d->pos + 1;
        int guard = 0;
        while (p + 6 <= d->size && guard++ < 200000) {
            if (d->data[p] == 0xFF && (d->data[p + 1] & 0xFE) == 0xF8 &&
                flac_frame_header(d->data, d->size, p, &d->si, &h)) {
                break;
            }
            p++;
        }
        if (p + 6 > d->size) {
            d->eof = 1;
            return 0;
        }
        d->pos = p;
        start = p;
    }
    br_init(&br, d->data + start, d->size - start);
    br_skip(&br, h.header_bytes * 8);
    bps = h.bps;

    for (ch = 0; ch < h.channels; ch++) {
        int sbps = bps;
        /* the "side" channel of the stereo modes carries one extra bit */
        if ((h.assignment == 8 && ch == 1) ||
            (h.assignment == 9 && ch == 0) ||
            (h.assignment == 10 && ch == 1))
            sbps = bps + 1;
        if (!flac_decode_subframe(&br, h.blocksize, sbps, d->chan[ch])) {
            d->err = 1;
            d->pos = start + 1;          /* try to resync next call */
            return -1;
        }
    }
    /* zero padding to byte boundary */
    if (br.bit & 7)
        br_skip(&br, 8 - (br.bit & 7));
    d->pos = start + br.bit / 8 + 2;     /* skip the CRC-16 footer */

    /* channel decorrelation + 16 bit conversion */
    {
        int32_t *c0 = d->chan[0];
        int32_t *c1 = h.channels > 1 ? d->chan[1] : d->chan[0];
        s16 *out = d->pcm;
        for (i = 0; i < h.blocksize; i++) {
            int32_t l, r;
            switch (h.assignment) {
            case 8:                       /* left / side */
                l = c0[i];
                r = c0[i] - c1[i];
                break;
            case 9:                       /* side / right */
                r = c1[i];
                l = c1[i] + c0[i];
                break;
            case 10: {                    /* mid / side */
                int32_t mid = c0[i];
                int32_t side = c1[i];
                mid = (mid << 1) | (side & 1);
                l = (mid + side) >> 1;
                r = (mid - side) >> 1;
                break;
            }
            default:
                l = c0[i];
                r = h.channels > 1 ? c1[i] : c0[i];
                break;
            }
            out[i * 2] = flac_to_s16(l, bps);
            out[i * 2 + 1] = flac_to_s16(r, bps);
        }
    }
    d->pcm_count = h.blocksize;
    d->pcm_pos = 0;
    d->cur_sample += h.blocksize;

    if (d->skip > 0) {
        int drop = (int)(d->skip < d->pcm_count ? d->skip : d->pcm_count);
        d->pcm_pos = drop;
        d->skip -= drop;
        d->cur_sample -= (h.blocksize - drop);
    }
    return 1;
}

static void flac_build_index(FlacDec *d)
{
    u32 p = d->si.audio_off;
    long long sample = 0;
    int frame_no = 0;

    d->indexed = 1;
    while (p + 6 <= d->size && d->idx_n < 2000000) {
        FrameHdr h;
        if (d->data[p] == 0xFF && (d->data[p + 1] & 0xFE) == 0xF8 &&
            flac_frame_header(d->data, d->size, p, &d->si, &h)) {
            unsigned long long expect = h.variable ? (unsigned long long)sample
                                                   : (unsigned long long)frame_no;
            if (h.number == expect) {
                if (d->idx_n >= d->idx_cap) {
                    int nc = d->idx_cap ? d->idx_cap * 2 : 256;
                    u32 *no = (u32 *)sy_realloc(d->idx_off, (u32)nc * sizeof(u32));
                    long long *ns;
                    if (!no)
                        return;
                    d->idx_off = no;
                    ns = (long long *)sy_realloc(d->idx_smp, (u32)nc * sizeof(long long));
                    if (!ns)
                        return;
                    d->idx_smp = ns;
                    d->idx_cap = nc;
                }
                d->idx_off[d->idx_n] = p;
                d->idx_smp[d->idx_n] = sample;
                d->idx_n++;
                sample += h.blocksize;
                frame_no++;
                p += h.header_bytes;
                continue;
            }
        }
        p++;
    }
}

static FlacDec *flac_open(const u8 *data, u32 size, FlacInfo *info)
{
    FlacDec *d;
    int ch;
    int maxblk;

    if (!flac_parse(data, size, info, 0, 0, 0, 0))
        return 0;
    d = (FlacDec *)sy_calloc(1, sizeof(FlacDec));
    if (!d)
        return 0;
    d->data = data;
    d->size = size;
    d->si = *info;
    d->pos = info->audio_off;
    d->cur_sample = 0;

    maxblk = info->max_blocksize > 0 ? info->max_blocksize : 4096;
    if (maxblk > FLAC_MAX_BLK)
        maxblk = FLAC_MAX_BLK;
    for (ch = 0; ch < info->channels && ch < FLAC_MAX_CH; ch++) {
        d->chan[ch] = (int32_t *)sy_alloc((u32)maxblk * sizeof(int32_t));
        if (!d->chan[ch])
            goto fail;
    }
    d->pcm = (s16 *)sy_alloc((u32)maxblk * 2 * sizeof(s16));
    if (!d->pcm)
        goto fail;
    return d;

fail:
    for (ch = 0; ch < FLAC_MAX_CH; ch++)
        if (d->chan[ch])
            sy_free(d->chan[ch]);
    if (d->pcm)
        sy_free(d->pcm);
    sy_free(d);
    return 0;
}

static int flac_read(FlacDec *d, s16 *out, int frames)
{
    int got = 0;
    int recover = 0;

    if (!d || frames <= 0)
        return 0;
    while (got < frames) {
        int n;
        if (d->pcm_pos >= d->pcm_count) {
            int r = flac_decode_next(d);
            if (r <= 0) {
                if (r == 0)
                    break;                       /* clean EOF */
                if (++recover > 8)
                    break;                       /* give up on a broken file */
                continue;
            }
            recover = 0;
        }
        n = d->pcm_count - d->pcm_pos;
        if (n > frames - got)
            n = frames - got;
        if (n > 0) {
            memcpy(out + got * 2, d->pcm + d->pcm_pos * 2, (size_t)n * 4);
            d->pcm_pos += n;
            got += n;
        }
    }
    return got;
}

static int flac_seek(FlacDec *d, long long sample)
{
    int lo, hi, best = 0;

    if (!d || sample < 0)
        return 0;
    if (d->si.total_samples > 0 && sample > d->si.total_samples)
        sample = d->si.total_samples;
    if (!d->indexed)
        flac_build_index(d);
    if (d->idx_n == 0) {
        d->pos = d->si.audio_off;
        d->cur_sample = 0;
        d->skip = sample;
        d->pcm_count = d->pcm_pos = 0;
        d->eof = 0;
        return 1;
    }
    lo = 0;
    hi = d->idx_n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (d->idx_smp[mid] <= sample) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    d->pos = d->idx_off[best];
    d->cur_sample = d->idx_smp[best];
    d->skip = sample - d->idx_smp[best];
    d->pcm_count = d->pcm_pos = 0;
    d->eof = 0;
    d->err = 0;
    return 1;
}

static void flac_close(FlacDec *d)
{
    int ch;
    if (!d)
        return;
    for (ch = 0; ch < FLAC_MAX_CH; ch++)
        if (d->chan[ch])
            sy_free(d->chan[ch]);
    if (d->pcm)
        sy_free(d->pcm);
    if (d->idx_off)
        sy_free(d->idx_off);
    if (d->idx_smp)
        sy_free(d->idx_smp);
    sy_free(d);
}


/* ------------------------------------------------------------------ */
/*  public wrappers used by decode.c / tags.c                          */
/* ------------------------------------------------------------------ */

int flac_probe(const u8 *data, u32 size, FlacInfo *info)
{
    return flac_parse(data, size, info, 0, 0, 0, 0);
}

int flac_meta(const u8 *data, u32 size, const u8 **vorbis, u32 *vorbis_len,
              const u8 **picture, u32 *picture_len)
{
    FlacInfo info;
    return flac_parse(data, size, &info, vorbis, vorbis_len, picture, picture_len);
}

FlacDec *flac_open_mem(const u8 *data, u32 size, FlacInfo *info)
{
    return flac_open(data, size, info);
}

int flac_read_pcm(FlacDec *d, s16 *out, int frames)
{
    return flac_read(d, out, frames);
}

int flac_seek_sample(FlacDec *d, long long sample)
{
    return flac_seek(d, sample);
}

void flac_close_mem(FlacDec *d)
{
    flac_close(d);
}
