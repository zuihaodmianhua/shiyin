/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* assoc.c -- file type registration.
 *
 * The right way to become a handler depends on the system:
 *
 *   Windows 95/98/ME   associations live in HKEY_CLASSES_ROOT and any program
 *   Windows 2000/XP    may write them; that is what we do there.
 *   Windows Vista/7    HKEY_CURRENT_USER\Software\Classes still takes effect.
 *   Windows 8 and up   the default handler is owned by the user and guarded by
 *                      a hash, so a program may only *register as a candidate*
 *                      (ProgID + Applications + Capabilities) and then send the
 *                      user to the system UI to pick it.  We open
 *                      ms-settings:defaultapps for that.
 *
 * assoc_set_default() therefore returns:
 *      0                  handled silently, we are the default now
 *      ASSOC_NEEDS_UI (1) registered as a candidate, system UI was opened
 *      anything else     Win32 error code
 */
#include "shiyin.h"
#include <winreg.h>
#include <shlobj.h>
#include <shellapi.h>

#define PROGID      "Shiyin.Audio"
#define APPNAME     "Shiyin.exe"
#define CAPABILITY  "Software\\Shiyin\\Capabilities"
#define REGAPP      "Software\\RegisteredApplications"

static const char *g_exts[] = {
    ".mp3", ".flac", ".wav", ".aiff", ".aif", ".aifc", ".au", ".snd", 0
};

static int g_keys_written;

/* ------------------------------------------------------------------ */
/*  OS detection (the manifest makes GetVersion tell the truth)        */
/* ------------------------------------------------------------------ */

static int os_major(void) { return (int)(GetVersion() & 0xFF); }
static int os_minor(void) { return (int)((GetVersion() >> 8) & 0xFF); }

static int is_win9x(void) { return os_major() < 5; }

static int is_modern(void)
{
    int major = os_major();
    if (major > 6)
        return 1;
    return major == 6 && os_minor() >= 2;      /* 8, 8.1, 10, 11 */
}

/* ------------------------------------------------------------------ */
/*  registry helpers                                                   */
/* ------------------------------------------------------------------ */

static HKEY class_root(void)
{
    return is_win9x() ? HKEY_CLASSES_ROOT : HKEY_CURRENT_USER;
}

static const char *class_prefix(void)
{
    return is_win9x() ? "" : "Software\\Classes\\";
}

static int reg_set_hk(HKEY root, const char *sub, const char *name, const char *value)
{
    HKEY k;
    DWORD disp;
    LONG r;

    r = (LONG)RegCreateKeyExA(root, sub, 0, "", 0, KEY_WRITE, NULL, &k, &disp);
    if (r != ERROR_SUCCESS) {
        sy_log_str("regkey", sub);
        sy_log("reg_create_err", (int)r, 0);
        return (int)r;
    }
    r = RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)value, (DWORD)strlen(value) + 1);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS)
        sy_log("reg_set_err", (int)r, 0);
    else
        g_keys_written++;
    return (int)r;
}

/* path relative to the classes root (".mp3", "Applications\\Shiyin.exe", ...) */
static int reg_set_class(const char *sub, const char *name, const char *value)
{
    char key[512];
    key[0] = 0;
    strncat(key, class_prefix(), sizeof(key) - 1);
    strncat(key, sub, sizeof(key) - strlen(key) - 1);
    return reg_set_hk(class_root(), key, name, value);
}

static int reg_set_hkcu(const char *sub, const char *name, const char *value)
{
    return reg_set_hk(HKEY_CURRENT_USER, sub, name, value);
}

static void reg_del_value_hk(HKEY root, const char *sub, const char *name)
{
    HKEY k;
    if (RegOpenKeyExA(root, sub, 0, KEY_READ | KEY_WRITE, &k) == ERROR_SUCCESS) {
        RegDeleteValueA(k, name);
        RegCloseKey(k);
    }
}

static int reg_get_hk(HKEY root, const char *sub, const char *name, char *out, int cap)
{
    HKEY k;
    DWORD type = 0, len = (DWORD)cap;
    LONG r;

    if (cap <= 0)
        return 0;
    out[0] = 0;
    if (RegOpenKeyExA(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return 0;
    r = RegQueryValueExA(k, name, NULL, &type, (LPBYTE)out, &len);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ)
        return 0;
    out[cap - 1] = 0;
    return 1;
}

/* Windows 95 cannot delete a key that still has subkeys, so walk bottom up. */
static void reg_del_tree_hk(HKEY root, const char *sub)
{
    HKEY k;

    if (RegOpenKeyExA(root, sub, 0, KEY_READ | KEY_WRITE, &k) == ERROR_SUCCESS) {
        for (;;) {
            char name[256], child[512];
            DWORD len = sizeof(name);
            FILETIME ft;
            if (RegEnumKeyExA(k, 0, name, &len, NULL, NULL, NULL, &ft) != ERROR_SUCCESS)
                break;
            child[0] = 0;
            strncat(child, sub, sizeof(child) - 1);
            strncat(child, "\\", sizeof(child) - strlen(child) - 1);
            strncat(child, name, sizeof(child) - strlen(child) - 1);
            reg_del_tree_hk(root, child);
        }
        RegCloseKey(k);
    }
    RegDeleteKeyA(root, sub);
}

static void reg_del_tree_class(const char *sub)
{
    char key[512];
    key[0] = 0;
    strncat(key, class_prefix(), sizeof(key) - 1);
    strncat(key, sub, sizeof(key) - strlen(key) - 1);
    reg_del_tree_hk(class_root(), key);
}

/* ------------------------------------------------------------------ */
/*  strings                                                            */
/* ------------------------------------------------------------------ */

static void exe_path(char *out, int cap)
{
    GetModuleFileNameA(NULL, out, cap);
    out[cap - 1] = 0;
}

static void open_command(char *out, int cap)
{
    char exe[SY_PATH];
    exe_path(exe, sizeof(exe));
    out[0] = 0;
    strncat(out, "\"", (size_t)(cap - 1));
    strncat(out, exe, (size_t)(cap - 1 - strlen(out)));
    strncat(out, "\" \"%1\"", (size_t)(cap - 1 - strlen(out)));
}

static void default_icon(char *out, int cap)
{
    char exe[SY_PATH];
    exe_path(exe, sizeof(exe));
    out[0] = 0;
    strncat(out, "\"", (size_t)(cap - 1));
    strncat(out, exe, (size_t)(cap - 1 - strlen(out)));
    strncat(out, "\",0", (size_t)(cap - 1 - strlen(out)));
}

static void friendly_name(char *out, int cap)
{
    w_ansi(L"拾音", out, cap);
    if (!out[0])
        strcpy(out, "Shiyin");
}

static void notify_shell(void)
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/*  shared pieces                                                      */
/* ------------------------------------------------------------------ */

/* ProgID + Applications entry + per-extension "Open with" registration.
 * Safe on every system: it never changes an existing default. */
int assoc_add_openwith(void)
{
    char sub[256], cmd[SY_PATH + 16], friendly[64];
    int i, rc = 0;

    open_command(cmd, sizeof(cmd));
    friendly_name(friendly, sizeof(friendly));

    strcpy(sub, "Applications\\");
    strncat(sub, APPNAME, sizeof(sub) - strlen(sub) - 1);
    rc = reg_set_class(sub, "", friendly);
    {
        char s2[256];
        strcpy(s2, sub);
        strncat(s2, "\\shell\\open\\command", sizeof(s2) - strlen(s2) - 1);
        if (!rc) rc = reg_set_class(s2, "", cmd);
        strcpy(s2, sub);
        strncat(s2, "\\DefaultIcon", sizeof(s2) - strlen(s2) - 1);
        if (!rc) {
            char icon[SY_PATH + 16];
            default_icon(icon, sizeof(icon));
            rc = reg_set_class(s2, "", icon);
        }
    }
    for (i = 0; g_exts[i] && !rc; i++) {
        char s2[256];
        strcpy(s2, sub);
        strncat(s2, "\\SupportedTypes", sizeof(s2) - strlen(s2) - 1);
        rc = reg_set_class(s2, g_exts[i], "");
    }
    if (!rc) {
        char desc[64];
        w_ansi(L"拾音音乐文件", desc, sizeof(desc));
        rc = reg_set_class(PROGID, "", desc);
    }
    if (!rc) {
        char s2[256], icon[SY_PATH + 16];
        strcpy(s2, PROGID);
        strncat(s2, "\\DefaultIcon", sizeof(s2) - strlen(s2) - 1);
        default_icon(icon, sizeof(icon));
        rc = reg_set_class(s2, "", icon);
    }
    if (!rc) {
        char s2[256];
        strcpy(s2, PROGID);
        strncat(s2, "\\shell\\open\\command", sizeof(s2) - strlen(s2) - 1);
        rc = reg_set_class(s2, "", cmd);
    }

    /* Win95 builds the "Open with" list from <ext>\OpenWithList */
    for (i = 0; g_exts[i] && !rc; i++) {
        char s2[256];
        strcpy(s2, g_exts[i]);
        strncat(s2, "\\OpenWithList", sizeof(s2) - strlen(s2) - 1);
        rc = reg_set_class(s2, APPNAME, "");
        if (!rc) {
            strcpy(s2, g_exts[i]);
            strncat(s2, "\\OpenWithProgids", sizeof(s2) - strlen(s2) - 1);
            rc = reg_set_class(s2, PROGID, "");
        }
    }

    if (!rc)
        notify_shell();
    sy_log("assoc_openwith", g_keys_written, rc);
    return rc;
}

/* Vista and newer prefer this over Applications entries: the "Default apps"
 * page lists programs that publish a Capabilities key. */
static int register_capabilities(void)
{
    char cmd[SY_PATH + 16], friendly[64], desc[128], icon[SY_PATH + 16];
    int i, rc = 0;

    open_command(cmd, sizeof(cmd));
    default_icon(icon, sizeof(icon));
    friendly_name(friendly, sizeof(friendly));
    w_ansi(L"拾音 · 本地音乐播放器", desc, sizeof(desc));

    {
        char s2[256];
        strcpy(s2, PROGID);
        strncat(s2, "\\shell\\open\\command", sizeof(s2) - strlen(s2) - 1);
        if (!rc) rc = reg_set_class(s2, "", cmd);
        strcpy(s2, PROGID);
        strncat(s2, "\\DefaultIcon", sizeof(s2) - strlen(s2) - 1);
        if (!rc) rc = reg_set_class(s2, "", icon);
    }
    if (!rc) rc = reg_set_hkcu(CAPABILITY, "ApplicationName", friendly);
    if (!rc) rc = reg_set_hkcu(CAPABILITY, "ApplicationDescription", desc);
    if (!rc) rc = reg_set_hkcu(CAPABILITY, "ApplicationIcon", icon);
    for (i = 0; g_exts[i] && !rc; i++) {
        char s2[256];
        strcpy(s2, CAPABILITY);
        strncat(s2, "\\FileAssociations", sizeof(s2) - strlen(s2) - 1);
        rc = reg_set_hkcu(s2, g_exts[i], PROGID);
    }
    if (!rc)
        rc = reg_set_hkcu(REGAPP, "Shiyin", CAPABILITY);
    sy_log("assoc_caps", g_keys_written, rc);
    return rc;
}

/* Open the place where the user can actually pick us. */
static int open_default_apps_ui(void)
{
    INT_PTR r;

    r = (INT_PTR)ShellExecuteA(NULL, "open", "ms-settings:defaultapps", NULL, NULL,
                               SW_SHOWNORMAL);
    if (r > 32)
        return 1;
    /* Vista / 7 fall back to the classic Default Programs control panel */
    r = (INT_PTR)ShellExecuteA(NULL, "open", "control.exe",
                               "/name Microsoft.DefaultPrograms", NULL, SW_SHOWNORMAL);
    return r > 32;
}

/* A process running at low integrity (sandbox, restricted token) cannot
 * write HKCU\Software\Classes at all: the key is medium integrity.  Detect
 * that so the UI can tell the user what to do instead of showing error 5. */
int assoc_integrity_low(void)
{
    HMODULE a;
    BOOL (WINAPI *p_open)(HANDLE, DWORD, PHANDLE);
    BOOL (WINAPI *p_get)(HANDLE, int, LPVOID, DWORD, LPDWORD);
    DWORD *(WINAPI *p_sub)(PSID, DWORD);
    UCHAR *(WINAPI *p_cnt)(PSID);
    HANDLE tok = NULL;
    char buf[256];
    int low = 0;

    a = LoadLibraryA("advapi32.dll");
    if (!a)
        return 0;
    p_open = (BOOL (WINAPI *)(HANDLE, DWORD, PHANDLE))GetProcAddress(a, "OpenProcessToken");
    p_get = (BOOL (WINAPI *)(HANDLE, int, LPVOID, DWORD, LPDWORD))GetProcAddress(a, "GetTokenInformation");
    p_sub = (DWORD *(WINAPI *)(PSID, DWORD))GetProcAddress(a, "GetSidSubAuthority");
    p_cnt = (UCHAR *(WINAPI *)(PSID))GetProcAddress(a, "GetSidSubAuthorityCount");
    if (p_open && p_get && p_sub && p_cnt &&
        p_open(GetCurrentProcess(), 0x0008 /* TOKEN_QUERY */, &tok)) {
        DWORD need = 0;
        p_get(tok, 25 /* TokenIntegrityLevel */, NULL, 0, &need);
        if (need > 0 && need <= sizeof(buf) &&
            p_get(tok, 25, buf, sizeof(buf), &need)) {
            PSID sid = *(PSID *)buf;          /* TOKEN_MANDATORY_LABEL.Label.Sid */
            UCHAR *count = p_cnt(sid);
            if (count && *count > 0) {
                DWORD *rid = p_sub(sid, (DWORD)(*count - 1));
                if (rid && *rid < 0x2000)     /* below SECURITY_MANDATORY_MEDIUM_RID */
                    low = 1;
            }
        }
        CloseHandle(tok);
    }
    FreeLibrary(a);
    return low;
}

/* ------------------------------------------------------------------ */
/*  public API                                                         */
/* ------------------------------------------------------------------ */

int assoc_set_default(void)
{
    int i, rc;

    if (is_modern()) {
        /* Windows 8+ ignores direct writes; register and let the user decide */
        rc = assoc_add_openwith();
        if (rc)
            return rc;
        rc = register_capabilities();
        if (rc)
            return rc;
        return open_default_apps_ui() ? ASSOC_NEEDS_UI : 2;
    }

    rc = assoc_add_openwith();
    if (rc)
        return rc;
    for (i = 0; g_exts[i] && !rc; i++)
        rc = reg_set_class(g_exts[i], "", PROGID);
    if (!rc)
        notify_shell();
    return rc;
}

int assoc_remove(void)
{
    int i;
    char cur[256], sub[256];

    for (i = 0; g_exts[i]; i++) {
        if (reg_get_hk(class_root(), g_exts[i], "", cur, sizeof(cur)) &&
            !strcmp(cur, PROGID))
            reg_del_value_hk(class_root(), g_exts[i], "");
    }
    reg_del_tree_class(PROGID);
    strcpy(sub, "Applications\\");
    strncat(sub, APPNAME, sizeof(sub) - strlen(sub) - 1);
    reg_del_tree_class(sub);

    reg_del_tree_hk(HKEY_CURRENT_USER, CAPABILITY);
    reg_del_value_hk(HKEY_CURRENT_USER, REGAPP, "Shiyin");
    notify_shell();
    return 0;
}

int assoc_is_default(void)
{
    char cur[256];

    if (is_modern()) {
        /* who owns .mp3 right now, according to the shell */
        if (reg_get_hk(HKEY_CURRENT_USER,
                       "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.mp3\\UserChoice",
                       "ProgId", cur, sizeof(cur)))
            return !strcmp(cur, PROGID);
        return 0;
    }
    return reg_get_hk(class_root(), ".mp3", "", cur, sizeof(cur)) &&
           !strcmp(cur, PROGID);
}
