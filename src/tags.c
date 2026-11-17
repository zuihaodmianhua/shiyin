/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* tags.c -- metadata + embedded cover extraction.
 *
 * Supported: ID3v1/1.1, ID3v2.2/2.3/2.4 (text frames, APIC/PIC), WAV LIST/INFO,
 * AIFF name/author chunks (plus an embedded ID3 chunk), AU (no tags).
 */
#include "shiyin.h"

/* ------------------------------------------------------------------ */
/*  helpers                                                            */
/* ------------------------------------------------------------------ */

static u32 id3_syncsafe(const u8 *p)
{
    return ((u32)(p[0] & 0x7F) << 21) | ((u32)(p[1] & 0x7F) << 14) |
           ((u32)(p[2] & 0x7F) << 7) | (u32)(p[3] & 0x7F);
}

static int id3_tag_size(const u8 *p, u32 size, u32 *body_off, u32 *body_len, int *ver, int *flags)
{
    if (size < 10 || p[0] != 'I' || p[1] != 'D' || p[2] != '3')
        return 0;
    if ((p[6] & 0x80) || (p[7] & 0x80) || (p[8] & 0x80) || (p[9] & 0x80))
        return 0;
    *ver = p[3];
    *flags = p[5];
    *body_off = 10;
    *body_len = id3_syncsafe(p + 6);
    if (*body_off + *body_len > size)
        *body_len = size - *body_off;
    return 1;
}

/* Remove ID3 unsynchronisation (0xFF 0x00 -> 0xFF). Returns new length. */
static u32 id3_unsync(const u8 *src, u32 len, u8 *dst, u32 cap)
{
    u32 i, o = 0;
    for (i = 0; i < len && o < cap; i++) {
        dst[o++] = src[i];
        if (src[i] == 0xFF && i + 1 < len && src[i + 1] == 0x00)
            i++;
    }
    return o;
}

static int latin1_len(const u8 *p, u32 max)
{
    u32 i = 0;
    while (i < max && p[i])
        i++;
    return (int)i;
}

static const wchar_t *genre_name(int idx)
{
    static const wchar_t *g[] = {
        L"Blues", L"Classic Rock", L"Country", L"Dance", L"Disco", L"Funk",
        L"Grunge", L"Hip-Hop", L"Jazz", L"Metal", L"New Age", L"Oldies",
        L"Other", L"Pop", L"R&B", L"Rap", L"Reggae", L"Rock", L"Techno",
        L"Industrial", L"Alternative", L"Ska", L"Death Metal", L"Pranks",
        L"Soundtrack", L"Euro-Techno", L"Ambient", L"Trip-Hop", L"Vocal",
        L"Jazz+Funk", L"Fusion", L"Trance", L"Classical", L"Instrumental",
        L"Acid", L"House", L"Game", L"Sound Clip", L"Gospel", L"Noise",
        L"AlternRock", L"Bass", L"Soul", L"Punk", L"Space", L"Meditative",
        L"Instrumental Pop", L"Instrumental Rock", L"Ethnic", L"Gothic",
        L"Darkwave", L"Techno-Industrial", L"Electronic", L"Pop-Folk",
        L"Eurodance", L"Dream", L"Southern Rock", L"Comedy", L"Cult",
        L"Gangsta", L"Top 40", L"Christian Rap", L"Pop/Funk", L"Jungle",
        L"Native American", L"Cabaret", L"New Wave", L"Psychedelic",
        L"Rave", L"Showtunes", L"Trailer", L"Lo-Fi", L"Tribal",
        L"Acid Punk", L"Acid Jazz", L"Polka", L"Retro", L"Musical",
        L"Rock & Roll", L"Hard Rock"
    };
    if (idx >= 0 && idx < (int)(sizeof(g) / sizeof(g[0])))
        return g[idx];
    return 0;
}

static void parse_genre(const wchar_t *src, wchar_t *dst, int cap)
{
    int i = 0, v = 0;
    if (src[0] == L'(') {
        i = 1;
        while (src[i] >= L'0' && src[i] <= L'9') {
            v = v * 10 + (src[i] - L'0');
            i++;
        }
        if (src[i] == L')') {
            const wchar_t *n = genre_name(v);
            if (n) {
                w_copy(dst, n, cap);
                return;
            }
            /* "17)Rock" style: keep the text after the paren */
            if (src[i + 1]) {
                w_copy(dst, src + i + 1, cap);
                return;
            }
        }
    }
    if (src[0] >= L'0' && src[0] <= L'9') {
        i = 0;
        v = 0;
        while (src[i] >= L'0' && src[i] <= L'9') {
            v = v * 10 + (src[i] - L'0');
            i++;
        }
        if (!src[i]) {
            const wchar_t *n = genre_name(v);
            if (n) {
                w_copy(dst, n, cap);
                return;
            }
        }
    }
    w_copy(dst, src, cap);
}

/* ------------------------------------------------------------------ */
/*  ID3 text frames                                                    */
/* ------------------------------------------------------------------ */

static void id3_text(const u8 *p, u32 len, wchar_t *out, int cap)
{
    int enc;
    if (!len || !cap)
        return;
    out[0] = 0;
    enc = p[0];
    p++;
    len--;
    switch (enc) {
    case 0:
        w_from_latin1((const char *)p, (int)len, out, cap);
        break;
    case 1:
        w_from_utf16(p, (int)len, 0, out, cap);
        break;
    case 2:
        w_from_utf16(p, (int)len, 1, out, cap);
        break;
    case 3:
        w_from_utf8((const char *)p, (int)len, out, cap);
        break;
    default:
        w_from_latin1((const char *)p, (int)len, out, cap);
        break;
    }
    /* frames may carry several values; keep the first */
    {
        int i;
        for (i = 0; out[i]; i++)
            ;
        if (i > 0 && out[i - 1] == L' ')
            out[i - 1] = 0;
    }
}

static void set_num(const wchar_t *s, int *dst)
{
    int i = 0, v = 0;
    while (s[i] >= L'0' && s[i] <= L'9') {
        v = v * 10 + (s[i] - L'0');
        i++;
    }
    if (i > 0)
        *dst = v;
}

static int pic_type_ok(int type)
{
    return type == 3;   /* front cover */
}

/* ------------------------------------------------------------------ */
/*  ID3 frame walk                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    const u8 *cover;
    u32       cover_len;
    int       cover_type;
} Id3Cover;

static void id3_frame(SyTrack *t, const char *id, const u8 *p, u32 len, Id3Cover *cov)
{
    if (len < 2)
        return;

    if (t) {
        if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) {
            if (!t->title[0]) id3_text(p, len, t->title, SY_TEXT);
        } else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) {
            if (!t->artist[0]) id3_text(p, len, t->artist, SY_TEXT);
        } else if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) {
            if (!t->album[0]) id3_text(p, len, t->album, SY_TEXT);
        } else if (!strcmp(id, "TPE2") || !strcmp(id, "TP2")) {
            if (!t->company[0]) id3_text(p, len, t->company, SY_TEXT);
        } else if (!strcmp(id, "TCON") || !strcmp(id, "TCO")) {
            wchar_t tmp[128];
            if (!t->genre[0]) {
                id3_text(p, len, tmp, 128);
                parse_genre(tmp, t->genre, 64);
            }
        } else if (!strcmp(id, "TYER") || !strcmp(id, "TYE")) {
            id3_text(p, len, t->year, 16);
        } else if (!strcmp(id, "TDRC")) {
            if (!t->year[0]) {
                wchar_t tmp[32];
                id3_text(p, len, tmp, 32);
                w_copy(t->year, tmp, 5);
            }
        } else if (!strcmp(id, "TRCK") || !strcmp(id, "TRK")) {
            wchar_t tmp[32];
            id3_text(p, len, tmp, 32);
            set_num(tmp, &t->track_no);
        } else if (!strcmp(id, "TPOS") || !strcmp(id, "TPA")) {
            wchar_t tmp[32];
            id3_text(p, len, tmp, 32);
            set_num(tmp, &t->disc_no);
        }
    }

    if (!cov)
        return;
    if (!strcmp(id, "APIC") || !strcmp(id, "PIC")) {
        int enc = p[0];
        u32 off = 1;
        int type = 0;
        const u8 *img;
        u32 img_len;

        if (!strcmp(id, "APIC")) {
            int mlen = latin1_len(p + off, len - off);
            off += (u32)mlen + 1;
        } else {
            off += 3;   /* 3 char image format */
        }
        if (off >= len)
            return;
        type = p[off++];
        /* description */
        if (enc == 1 || enc == 2) {
            while (off + 1 < len && !(p[off] == 0 && p[off + 1] == 0))
                off += 2;
            off += 2;
        } else {
            while (off < len && p[off])
                off++;
            off++;
        }
        if (off >= len)
            return;
        img = p + off;
        img_len = len - off;
        if (!img_len)
            return;
        if (!cov->cover || (pic_type_ok(type) && !pic_type_ok(cov->cover_type))) {
            cov->cover = img;
            cov->cover_len = img_len;
            cov->cover_type = type;
        }
    }
}

static void parse_id3(SyTrack *t, const u8 *data, u32 size, Id3Cover *cov)
{
    u32 off, len, frame_off;
    int ver, flags;
    u8 *tmp = 0;

    if (!id3_tag_size(data, size, &off, &len, &ver, &flags))
        return;
    data += off;
    size = len;

    if ((ver == 2 || ver == 3) && (flags & 0x80)) {
        tmp = (u8 *)sy_alloc(size + 16);
        if (!tmp)
            return;
        size = id3_unsync(data, size, tmp, size);
        data = tmp;
        flags &= ~0x80;
    }

    if (ver == 3 && (flags & 0x40)) {           /* extended header */
        u32 eh = ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | data[3];
        if (eh + 4 <= size)
            data += 4 + eh, size -= 4 + eh;
    } else if (ver == 4 && (flags & 0x40)) {
        u32 eh = id3_syncsafe(data);
        if (eh && eh <= size)
            data += eh, size -= eh;
    }

    frame_off = 0;
    while (frame_off + (ver == 2 ? 6u : 10u) <= size) {
        const u8 *fp = data + frame_off;
        char id[8];
        u32 flen;
        int fflags = 0;
        u32 hdr;

        if (fp[0] == 0)
            break;
        memset(id, 0, sizeof(id));
        if (ver == 2) {
            id[0] = (char)fp[0];
            id[1] = (char)fp[1];
            id[2] = (char)fp[2];
            flen = ((u32)fp[3] << 16) | ((u32)fp[4] << 8) | fp[5];
            hdr = 6;
        } else {
            id[0] = (char)fp[0];
            id[1] = (char)fp[1];
            id[2] = (char)fp[2];
            id[3] = (char)fp[3];
            if (ver == 4)
                flen = id3_syncsafe(fp + 4);
            else
                flen = ((u32)fp[4] << 24) | ((u32)fp[5] << 16) | ((u32)fp[6] << 8) | fp[7];
            fflags = (fp[8] << 8) | fp[9];
            hdr = 10;
        }
        if (!flen || frame_off + hdr + flen > size)
            break;

        {
            const u8 *body = fp + hdr;
            u32 blen = flen;
            u8 *ftmp = 0;

            if (ver == 3) {
                if (fflags & 0x0080) { /* compressed */ goto next_frame; }
                if (fflags & 0x0040) { /* encrypted  */ goto next_frame; }
                if (fflags & 0x0020) { body++; blen--; }         /* grouping */
            } else if (ver == 4) {
                if (fflags & 0x000C) { goto next_frame; }        /* compressed/encrypted */
                if (fflags & 0x0040) { body++; blen--; }         /* grouping */
                if (fflags & 0x0001) { body += 4; blen -= 4; }   /* data length */
                if (fflags & 0x0002) {                           /* per-frame unsync */
                    ftmp = (u8 *)sy_alloc(blen + 16);
                    if (ftmp) {
                        blen = id3_unsync(body, blen, ftmp, blen);
                        body = ftmp;
                    }
                }
            }
            if (blen > 0 && blen < 0x8000000u)
                id3_frame(t, id, body, blen, cov);
            if (ftmp)
                sy_free(ftmp);
        }
    next_frame:
        frame_off += hdr + flen;
    }
    if (tmp)
        sy_free(tmp);
}

/* ------------------------------------------------------------------ */
/*  ID3v1                                                              */
/* ------------------------------------------------------------------ */

static void parse_id3v1(SyTrack *t, const u8 *p)
{
    wchar_t tmp[64];
    if (memcmp(p, "TAG", 3))
        return;
    if (!t->title[0]) {
        w_from_ansi((const char *)p + 3, tmp, 30);
        w_trim(tmp);
        w_copy(t->title, tmp, SY_TEXT);
    }
    if (!t->artist[0]) {
        w_from_ansi((const char *)p + 33, tmp, 30);
        w_trim(tmp);
        w_copy(t->artist, tmp, SY_TEXT);
    }
    if (!t->album[0]) {
        w_from_ansi((const char *)p + 63, tmp, 30);
        w_trim(tmp);
        w_copy(t->album, tmp, SY_TEXT);
    }
    if (!t->year[0]) {
        w_from_ansi((const char *)p + 93, tmp, 8);
        w_trim(tmp);
        w_copy(t->year, tmp, 16);
    }
    if (p[125] == 0 && p[126]) {
        t->track_no = p[126];
    }
}

/* ------------------------------------------------------------------ */
/*  RIFF / WAV info                                                    */
/* ------------------------------------------------------------------ */

static u32 fcc(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static int key_eq(const u8 *p, u32 n, const char *s)
{
    u32 i;
    for (i = 0; i < n; i++) {
        char a = (char)p[i];
        if (!s[i])
            return 0;
        if (a >= 'a' && a <= 'z')
            a = (char)(a - 32);
        if (a != s[i])
            return 0;
    }
    return s[n] == 0;
}

/* FLAC / Ogg style "KEY=VALUE" comment block. */
static void parse_vorbis_tags(SyTrack *t, const u8 *p, u32 len)
{
    u32 off, count, i;

    if (len < 8)
        return;
    off = 4 + le32(p);                 /* skip vendor string */
    if (off + 4 > len)
        return;
    count = le32(p + off);
    off += 4;
    for (i = 0; i < count && i < 1024 && off + 4 <= len; i++) {
        u32 n = le32(p + off);
        const u8 *kv;
        u32 eq = 0;
        wchar_t val[SY_TEXT];

        off += 4;
        if (off + n > len)
            break;
        kv = p + off;
        while (eq < n && kv[eq] != '=')
            eq++;
        if (eq < n) {
            u32 vlen = n - eq - 1;
            int is = 0;
#define KEY_IS(s) key_eq(kv, eq, s)
            if (vlen >= sizeof(val))
                vlen = sizeof(val) - 1;
            w_from_utf8((const char *)kv + eq + 1, (int)vlen, val, SY_TEXT);
            w_trim(val);
            if (KEY_IS("TITLE")) {
                if (!t->title[0]) w_copy(t->title, val, SY_TEXT);
            } else if (KEY_IS("ARTIST")) {
                if (!t->artist[0]) w_copy(t->artist, val, SY_TEXT);
            } else if (KEY_IS("ALBUM")) {
                if (!t->album[0]) w_copy(t->album, val, SY_TEXT);
            } else if (KEY_IS("ALBUMARTIST") || KEY_IS("ALBUM ARTIST")) {
                if (!t->company[0]) w_copy(t->company, val, SY_TEXT);
            } else if (KEY_IS("DATE") || KEY_IS("YEAR")) {
                if (!t->year[0]) { val[4] = 0; w_copy(t->year, val, 16); }
            } else if (KEY_IS("GENRE")) {
                if (!t->genre[0]) w_copy(t->genre, val, 64);
            } else if (KEY_IS("TRACKNUMBER")) {
                set_num(val, &t->track_no);
            } else if (KEY_IS("DISCNUMBER")) {
                set_num(val, &t->disc_no);
            }
#undef KEY_IS
            (void)is;
        }
        off += n;
    }
}

/* FLAC PICTURE metadata block:  type/mime/description/w/h/depth/colors/data */
static int flac_picture(const u8 *p, u32 len, u8 **out, int *outlen)
{
    u32 off = 4, mlen, dlen, plen;

    if (len < 32)
        return 0;
    mlen = be32(p + off);
    off += 4;
    if (off + mlen + 4 > len)
        return 0;
    off += mlen;                       /* mime type */
    dlen = be32(p + off);
    off += 4;
    if (off + dlen + 20 > len)
        return 0;
    off += dlen + 16;                  /* description, width/height/depth/colors */
    plen = be32(p + off);
    off += 4;
    if (!plen || off + plen > len || plen > 32u * 1024u * 1024u)
        return 0;
    {
        u8 *buf = (u8 *)sy_alloc(plen);
        if (!buf)
            return 0;
        memcpy(buf, p + off, plen);
        *out = buf;
        *outlen = (int)plen;
    }
    return 1;
}

static void parse_wav_info(SyTrack *t, const u8 *data, u32 size)
{
    u32 off = 12;
    while (off + 8 <= size) {
        u32 id = fcc(data + off);
        u32 len = (u32)data[off + 4] | ((u32)data[off + 5] << 8) |
                  ((u32)data[off + 6] << 16) | ((u32)data[off + 7] << 24);
        u32 body = off + 8;
        if (body + len > size)
            len = size - body;
        if (id == fcc((const u8 *)"LIST") && len > 4) {
            u32 typ = fcc(data + body);
            if (typ == fcc((const u8 *)"INFO") || typ == fcc((const u8 *)"info")) {
                u32 q = body + 4;
                while (q + 8 <= body + len) {
                    u32 sid = fcc(data + q);
                    u32 slen = (u32)data[q + 4] | ((u32)data[q + 5] << 8) |
                               ((u32)data[q + 6] << 16) | ((u32)data[q + 7] << 24);
                    u32 sbody = q + 8;
                    wchar_t tmp[SY_TEXT];
                    if (sbody + slen > size)
                        break;
                    w_from_ansi((const char *)data + sbody, tmp, SY_TEXT);
                    w_trim(tmp);
                    if (sid == fcc((const u8 *)"INAM") && !t->title[0])
                        w_copy(t->title, tmp, SY_TEXT);
                    else if (sid == fcc((const u8 *)"IART") && !t->artist[0])
                        w_copy(t->artist, tmp, SY_TEXT);
                    else if (sid == fcc((const u8 *)"IPRD") && !t->album[0])
                        w_copy(t->album, tmp, SY_TEXT);
                    else if (sid == fcc((const u8 *)"ICRD") && !t->year[0])
                        w_copy(t->year, tmp, 16);
                    else if (sid == fcc((const u8 *)"IGNR") && !t->genre[0])
                        w_copy(t->genre, tmp, 64);
                    else if (sid == fcc((const u8 *)"ITRK")) {
                        set_num(tmp, &t->track_no);
                    } else if ((sid == fcc((const u8 *)"ID3 ") || sid == fcc((const u8 *)"id3 ")) && slen > 10) {
                        parse_id3(t, data + sbody, slen, 0);
                    }
                    q = sbody + slen + (slen & 1);
                }
            }
        } else if (id == fcc((const u8 *)"id3 ") || id == fcc((const u8 *)"ID3 ")) {
            parse_id3(t, data + body, len, 0);
        }
        off = body + len + (len & 1);
    }
}

/* ------------------------------------------------------------------ */
/*  AIFF chunks                                                        */
/* ------------------------------------------------------------------ */

static void parse_aiff_tags(SyTrack *t, const u8 *data, u32 size)
{
    u32 off = 12;
    while (off + 8 <= size) {
        u32 id = fcc(data + off);
        u32 len = ((u32)data[off + 4] << 24) | ((u32)data[off + 5] << 16) |
                  ((u32)data[off + 6] << 8) | data[off + 7];
        u32 body = off + 8;
        if (body + len > size)
            len = size - body;
        if (id == fcc((const u8 *)"NAME") && !t->title[0]) {
            w_from_latin1((const char *)data + body, (int)len, t->title, SY_TEXT);
            w_trim(t->title);
        } else if (id == fcc((const u8 *)"AUTH") && !t->artist[0]) {
            w_from_latin1((const char *)data + body, (int)len, t->artist, SY_TEXT);
            w_trim(t->artist);
        } else if (id == fcc((const u8 *)"ANNO") && !t->album[0]) {
            w_from_latin1((const char *)data + body, (int)len, t->album, SY_TEXT);
            w_trim(t->album);
        } else if (id == fcc((const u8 *)"ID3 ") || id == fcc((const u8 *)"id3 ")) {
            parse_id3(t, data + body, len, 0);
        }
        off = body + len + (len & 1);
    }
}

/* ------------------------------------------------------------------ */
/*  public API                                                         */
/* ------------------------------------------------------------------ */

int tags_probe(SyTrack *t)
{
    SyFile f;
    const u8 *p;
    u32 size;
    Id3Cover cov;
    int is_id3 = 0;

    memset(&cov, 0, sizeof(cov));
    t->file_size = (unsigned int)file_size_of(t->path);
    if (!audio_probe(t->path, t)) {
        /* keep the row, mark unknown */
        path_base(t->path, t->title, SY_TEXT);
        t->format = FMT_UNKNOWN;
        return 0;
    }
    if (!file_map(t->path, &f))
        return 1;
    p = (const u8 *)f.base;
    size = f.size;

    if (t->format == FMT_MP3) {
        parse_id3(t, p, size, &cov);
        is_id3 = 1;
        if (size > 128)
            parse_id3v1(t, p + size - 128);
    } else if (t->format == FMT_WAV) {
        parse_wav_info(t, p, size);
    } else if (t->format == FMT_AIFF) {
        parse_aiff_tags(t, p, size);
    } else if (t->format == FMT_FLAC) {
        const u8 *vc = 0, *pic = 0;
        u32 vclen = 0, piclen = 0;
        if (flac_meta(p, size, &vc, &vclen, &pic, &piclen)) {
            if (vc)
                parse_vorbis_tags(t, vc, vclen);
            if (pic)
                t->has_art = 1;
        }
    }
    if (is_id3 && cov.cover)
        t->has_art = 1;
    file_unmap(&f);

    if (!t->title[0]) {
        wchar_t base[SY_TEXT];
        int i, n;
        path_base(t->path, base, SY_TEXT);
        n = w_len(base);
        for (i = n - 1; i > 0; i--) {
            if (base[i] == L'.') {
                base[i] = 0;
                break;
            }
        }
        w_copy(t->title, base, SY_TEXT);
    }
    return 1;
}

int tags_cover(const char *path, u8 **data, int *len)
{
    SyFile f;
    const u8 *p;
    u32 size;
    Id3Cover cov;
    int got = 0;

    *data = 0;
    *len = 0;
    if (!file_map(path, &f))
        return 0;
    p = (const u8 *)f.base;
    size = f.size;
    memset(&cov, 0, sizeof(cov));

    if (size > 4 && p[0] == 'f' && p[1] == 'L' && p[2] == 'a' && p[3] == 'C') {
        const u8 *vc = 0, *pic = 0;
        u32 vclen = 0, piclen = 0;
        if (flac_meta(p, size, &vc, &vclen, &pic, &piclen) && pic) {
            if (flac_picture(pic, piclen, data, len)) {
                file_unmap(&f);
                return 1;
            }
        }
    } else if (size > 12 && fcc(p) == fcc((const u8 *)"RIFF")) {
        u32 off = 12;
        while (off + 8 <= size) {
            u32 id = fcc(p + off);
            u32 clen = (u32)p[off + 4] | ((u32)p[off + 5] << 8) |
                       ((u32)p[off + 6] << 16) | ((u32)p[off + 7] << 24);
            u32 body = off + 8;
            if (body + clen > size)
                clen = size - body;
            if (id == fcc((const u8 *)"id3 ") || id == fcc((const u8 *)"ID3 ")) {
                parse_id3(0, p + body, clen, &cov);
                if (cov.cover)
                    break;
            }
            off = body + clen + (clen & 1);
        }
    } else if (size > 12 && fcc(p) == fcc((const u8 *)"FORM")) {
        u32 off = 12;
        while (off + 8 <= size) {
            u32 id = fcc(p + off);
            u32 clen = ((u32)p[off + 4] << 24) | ((u32)p[off + 5] << 16) |
                       ((u32)p[off + 6] << 8) | p[off + 7];
            u32 body = off + 8;
            if (body + clen > size)
                clen = size - body;
            if (id == fcc((const u8 *)"ID3 ") || id == fcc((const u8 *)"id3 ")) {
                parse_id3(0, p + body, clen, &cov);
                if (cov.cover)
                    break;
            }
            off = body + clen + (clen & 1);
        }
    } else {
        parse_id3(0, p, size, &cov);
    }

    if (cov.cover && cov.cover_len && cov.cover_len < 32u * 1024u * 1024u) {
        u8 *buf = (u8 *)sy_alloc(cov.cover_len);
        if (buf) {
            memcpy(buf, cov.cover, cov.cover_len);
            *data = buf;
            *len = (int)cov.cover_len;
            got = 1;
        }
    }
    file_unmap(&f);
    return got;
}


