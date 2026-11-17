/* dec_test.c -- offline decoder harness (not part of the app).
 *
 *   dec_test.exe <input audio> <output.wav>
 *
 * Decodes through the same dec_open/dec_read path the player uses and writes
 * a 16 bit stereo WAV so the result can be compared bit for bit.
 */
#include "../src/shiyin.h"

static char *skip_arg(char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '"') {
        p++;
        while (*p && *p != '"')
            p++;
        if (*p)
            p++;
    } else {
        while (*p && *p != ' ' && *p != '\t')
            p++;
    }
    return p;
}

static void copy_arg(char *p, char *dst, int cap)
{
    int i = 0;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '"')
        p++;
    while (*p && *p != ' ' && *p != '\t' && *p != '"' && i < cap - 1)
        dst[i++] = *p++;
    dst[i] = 0;
}

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static void put16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

void sy_entry(void)
{
    char *cmd = GetCommandLineA();
    char in[SY_PATH], out[SY_PATH], arg[32];
    int seek_ms = 0;
    SyDec *d;
    SyAudio info;
    HANDLE h;
    u8 hdr[44];
    u32 total = 0;
    s16 buf[4096 * 2];
    int n;
    DWORD wr;

    cmd = skip_arg(cmd);            /* program name */
    copy_arg(cmd, in, SY_PATH);
    cmd = skip_arg(cmd);
    copy_arg(cmd, out, SY_PATH);
    cmd = skip_arg(cmd);
    copy_arg(cmd, arg, sizeof(arg));
    {
        int i = 0, v = 0;
        while (arg[i] >= '0' && arg[i] <= '9') {
            v = v * 10 + (arg[i] - '0');
            i++;
        }
        seek_ms = v;
    }
    if (!in[0] || !out[0])
        ExitProcess(2);

    memset(&info, 0, sizeof(info));
    d = dec_open(in, &info);
    if (!d)
        ExitProcess(3);
    if (seek_ms > 0)
        dec_seek(d, seek_ms);

    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "RIFF", 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    put32(hdr + 16, 16);
    put16(hdr + 20, 1);
    put16(hdr + 22, 2);
    put32(hdr + 24, (u32)info.rate);
    put32(hdr + 28, (u32)info.rate * 4);
    put16(hdr + 32, 4);
    put16(hdr + 34, 16);
    memcpy(hdr + 36, "data", 4);

    h = CreateFileA(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        dec_close(d);
        ExitProcess(4);
    }
    WriteFile(h, hdr, 44, &wr, NULL);
    for (;;) {
        n = dec_read(d, buf, 4096);
        if (n <= 0)
            break;
        WriteFile(h, buf, (DWORD)n * 4, &wr, NULL);
        total += (u32)n * 4;
    }
    put32(hdr + 4, 36 + total);
    put32(hdr + 40, total);
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    WriteFile(h, hdr, 44, &wr, NULL);
    CloseHandle(h);
    dec_close(d);
    ExitProcess(0);
}
