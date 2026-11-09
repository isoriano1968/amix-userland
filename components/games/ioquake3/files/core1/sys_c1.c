/*
 * sys_c1.c -- ioquake3's system layer on the Z3660's core 1, in place of
 * sys/sys_main.c, sys/sys_unix.c and the consoles: no OS, no SDL, one
 * thread.  Files are core 1's memory files (c1fs.c), the console is AMIX's
 * log ring, time is core 1's clock.
 *
 * The game modules (cgame, qagame, ui) are linked into the image (no
 * dlopen on bare metal); each is one relocatable object with its globals
 * in its own .data/.bss (files/core1/module.ld).  A "load" gives them back
 * their first values, as a fresh dlopen would, so a module that counts on
 * zeroed globals at every map behaves as on Linux.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "c1q3.h"
#include "c1fs.h"

/* --- the game modules --- */
typedef void (QDECL *dll_entry_t)(intptr_t (QDECL *)(intptr_t, ...));

#define MODULE_SYMS(m) \
    extern void m##_dllEntry(intptr_t (QDECL *)(intptr_t, ...)); \
    extern intptr_t m##_vmMain(int, int, int, int, int, int, int, int, int, int, int, int, int); \
    extern char m##_data_start[], m##_data_end[], m##_bss_start[], m##_bss_end[];
MODULE_SYMS(cgame)
MODULE_SYMS(qagame)
MODULE_SYMS(ui)

static struct module {
    const char *name;
    dll_entry_t entry;
    vmMainProc main;
    char *data, *data_end, *bss, *bss_end;
    char *first;                        /* .data as it was before the first load */
} modules[] = {
#define M(m) { #m, m##_dllEntry, (vmMainProc)m##_vmMain, m##_data_start, m##_data_end, m##_bss_start, m##_bss_end, NULL }
    M(cgame), M(qagame), M(ui)
#undef M
};

void *QDECL Sys_LoadGameDll(const char *name, vmMainProc *entryPoint, intptr_t (QDECL *systemcalls)(intptr_t, ...))
{
    const char *base = strrchr(name, '/');
    size_t n, i;

    base = base ? base + 1 : name;
    n = strcspn(base, ".");
    for (i = 0; i < ARRAY_LEN(modules); i++) {
        struct module *m = &modules[i];
        const size_t ds = (size_t)(m->data_end - m->data);

        if (strlen(m->name) != n || strncmp(m->name, base, n))
            continue;
        if (!m->first) {
            m->first = malloc(ds ? ds : 1);
            if (!m->first)
                return NULL;
            memcpy(m->first, m->data, ds);
        } else
            memcpy(m->data, m->first, ds);
        memset(m->bss, 0, (size_t)(m->bss_end - m->bss));
        Com_Printf("Sys_LoadGameDll(%s): linked in (%u + %u bytes of globals)\n", name, (unsigned)ds,
                   (unsigned)(m->bss_end - m->bss));
        m->entry(systemcalls);
        *entryPoint = m->main;
        return m;
    }
    Com_Printf("Sys_LoadGameDll(%s): not in this image\n", name);
    return NULL;
}

void Sys_UnloadDll(void *dllHandle)
{
    (void)dllHandle;
}

void *QDECL Sys_LoadDll(const char *name, qboolean useSystemLib)
{
    (void)name; (void)useSystemLib;
    return NULL;
}

qboolean Sys_DllExtension(const char *name)
{
    return COM_CompareExtension(name, DLL_EXT);
}

/* FS_FindVM looks for the module's file in each game directory: a byte
   stands in for it under /base/<game>/ */
static void module_files(void)
{
    char dirs[16][MAX_QPATH];
    int nd = 0, i, j, k;

    for (i = 0; i < C1FS_MAX; i++) {
        const char *p = c1fs[i].name, *e;

        if (!p || strncmp(p, "/base/", 6) || !(e = strchr(p + 6, '/')) || e - (p + 6) >= MAX_QPATH)
            continue;
        for (j = 0; j < nd; j++)
            if (!strncmp(dirs[j], p + 6, (size_t)(e - (p + 6))) && !dirs[j][e - (p + 6)])
                break;
        if (j == nd && nd < 16) {
            memcpy(dirs[nd], p + 6, (size_t)(e - (p + 6)));
            dirs[nd++][e - (p + 6)] = 0;
        }
    }
    for (j = 0; j < nd; j++)
        for (k = 0; k < (int)ARRAY_LEN(modules); k++) {
            char path[MAX_OSPATH];
            int f;

#ifdef C1_WOLF
            Com_sprintf(path, sizeof(path), "/base/%s/%s", dirs[j], Sys_GetDLLName(modules[k].name));
#else
            Com_sprintf(path, sizeof(path), "/base/%s/%s%s", dirs[j], modules[k].name, DLL_EXT);
#endif
            if (c1fs_find(path) < 0 && (f = c1fs_add(path, 1, 0)) >= 0)
                c1fs[f].data[0] = 0;
        }
}

#ifdef C1_WOLF
/* iortcw names its modules cgame.sp.arm.so ... */
char *Sys_GetDLLName(const char *name)
{
    return va("%s.sp." ARCH_STRING DLL_EXT, name);
}

/* iortcw's own system calls: one home, no other programs, a fast enough CPU */
char *Sys_DefaultHomePath(void) { return "/home"; }
void Sys_StartProcess(char *cmdline, qboolean doexit) { (void)cmdline; (void)doexit; }
void Sys_OpenURL(char *url, qboolean doexit) { (void)url; (void)doexit; }
int Sys_GetHighQualityCPU(void) { return 1; }
#endif

/* --- console, errors, exit --- */
void Sys_Print(const char *msg)
{
    char buf[1024];
    size_t n = 0;

    for (; *msg; msg++) {
        if (Q_IsColorString(msg)) {     /* ^1 and the like: not for a terminal */
            msg++;
            continue;
        }
        buf[n++] = *msg;
        if (n == sizeof(buf) - 1) {
            buf[n] = 0;
            c1p_log(buf);
            n = 0;
        }
    }
    buf[n] = 0;
    c1p_log(buf);
}

void QDECL Sys_Error(const char *error, ...)
{
    va_list ap;
    char text[1024];

    va_start(ap, error);
    Q_vsnprintf(text, sizeof(text), error, ap);
    va_end(ap);
    c1p_fail(text);
}

/* why softpipe's fast path refused draws (sp_fast.c in amix-gate's Mesa:
   counts per reason, since the start) */
extern unsigned sp_fast_why[16];

static void why_f(void)
{
    char line[256];
    int k, n = 0;

    n += Com_sprintf(line + n, sizeof(line) - n, "sp_fast refused (reason:draws):");
    for (k = 1; k < 16; k++)
        if (sp_fast_why[k])
            n += Com_sprintf(line + n, sizeof(line) - n, " %d:%u", k, sp_fast_why[k]);
    Com_Printf("%s\n", line);
}

/* softpipe's own profile (sp_fast.h): cycles per part, spans, pixels, L1
   refills; printed and cleared.  "c1prof on|off": the refill counts too */
extern uint64_t sp_fast_prof[16];
extern int sp_fast_evt_on;

static void prof_f(void)
{
    static const char *const names[16] = { "fasttri_Mc", "spans_Mc", "prepare_Mc", "drawvbo_Mc", "spans",
        "pixels", "swtris", "swtris_Mc", "l1refill", "px_affine", "px_persp", "px_other", "texsetup_Mc",
        "segments", "evt1", "px_pairs" };
    static const int mc[16] = { 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0 };
    char line[512];
    int k, n = 0;

    if (Cmd_Argc() > 1) {
        sp_fast_evt_on = !Q_stricmp(Cmd_Argv(1), "on");
        return;
    }
    n += Com_sprintf(line + n, sizeof(line) - n, "sp_fast_prof:");
    for (k = 0; k < 16; k++) {
        n += Com_sprintf(line + n, sizeof(line) - n, " %s=%llu", names[k],
                         (unsigned long long)(mc[k] ? sp_fast_prof[k] / 1000000u : sp_fast_prof[k]));
        sp_fast_prof[k] = 0;
    }
    Com_Printf("%s\n", line);
}

/* "c1mem": load latency through core 1's heap: a random pointer chase over
   16 KB .. 16 MB (L1 32 KB, the shared L2 512 KB, then DDR), ns and cycles
   a load on core 1's clock */
static void mem_f(void)
{
    static const unsigned sizes[] = { 16, 64, 128, 256, 512, 1024, 4096, 16384 };
    unsigned k;

    for (k = 0; k < ARRAY_LEN(sizes); k++) {
        const unsigned n = sizes[k] * 1024 / 32;    /* one pointer per 32-byte line */
        uint32_t *buf = malloc((size_t)n * 32 + 32), *p;
        unsigned i, *perm;
        uint64_t t0, t1;
        const unsigned loads = 2000000;

        perm = malloc(n * sizeof(*perm));
        if (!buf || !perm) {
            free(buf);
            free(perm);
            Com_Printf("c1mem: no memory for %u KB\n", sizes[k]);
            continue;
        }
        for (i = 0; i < n; i++)
            perm[i] = i;
        for (i = n - 1; i > 0; i--) {               /* a random cycle through the lines */
            const unsigned j = (unsigned)rand() % (i + 1), t = perm[i];

            perm[i] = perm[j];
            perm[j] = t;
        }
        for (i = 0; i < n; i++)
            *(uint32_t **)(buf + perm[i] * 8) = buf + perm[(i + 1) % n] * 8;
        p = buf + perm[0] * 8;
        for (i = 0; i < n; i++)                     /* warm */
            p = *(uint32_t **)p;
        t0 = c1p_now_us();
        for (i = 0; i < loads; i++)
            p = *(uint32_t **)p;
        t1 = c1p_now_us();
        Com_Printf("c1mem %6u KB: %.1f ns a load (%p)\n", sizes[k], (double)(t1 - t0) * 1000.0 / loads, (void *)p);
        free(perm);
        free(buf);
    }
}

void Sys_Quit(void)
{
    why_f();
    c1p_quit();
}

dialogResult_t Sys_Dialog(dialogType_t type, const char *message, const char *title)
{
    (void)type;
    Sys_Print(va("%s: %s\n", title, message));
    return DR_OK;
}

void Sys_ErrorDialog(const char *error);
void Sys_ErrorDialog(const char *error)
{
    Sys_Print(va("error: %s\n", error));
}

char *Sys_ConsoleInput(void)
{
    return NULL;
}

char *Sys_GetClipboardData(void)
{
    return NULL;
}

static void Sys_In_Restart_f(void)
{
    IN_Restart();
}

void Sys_Init(void)
{
    Cmd_AddCommand("in_restart", Sys_In_Restart_f);
    Cmd_AddCommand("c1why", why_f);
    Cmd_AddCommand("c1prof", prof_f);
    Cmd_AddCommand("c1mem", mem_f);
    Cvar_Set("arch", OS_STRING " " ARCH_STRING);
    Cvar_Set("username", Sys_GetCurrentUser());
}

cpuFeatures_t Sys_GetProcessorFeatures(void)
{
    return (cpuFeatures_t)0;
}

void Sys_GLimpSafeInit(void) { }
void Sys_GLimpInit(void) { }
void Sys_SetFloatEnv(void) { }
void Sys_PlatformInit(void) { }
void Sys_PlatformExit(void) { }
void Sys_SetEnv(const char *name, const char *value)
{
    if (value && *value)
        setenv(name, value, 1);
    else
        unsetenv(name);
}
int Sys_PID(void) { return 1; }
qboolean Sys_PIDIsRunning(int pid) { (void)pid; return qfalse; }
void Sys_InitPIDFile(const char *gamedir) { (void)gamedir; }
void Sys_RemovePIDFile(const char *gamedir) { (void)gamedir; }
qboolean Sys_OpenFolderInFileManager(const char *path, qboolean create) { (void)path; (void)create; return qfalse; }
qboolean Sys_LowPhysicalMemory(void) { return qfalse; }
char *Sys_GetCurrentUser(void) { return "player"; }

qboolean Sys_RandomBytes(byte *string, int len)
{
    (void)string; (void)len;
    return qfalse;                      /* Com_RandomBytes falls back on rand() */
}

/* --- time --- */
int Sys_Milliseconds(void)
{
    static uint64_t base;
    const uint64_t now = c1p_now_us();

    if (!base)
        base = now;
    return (int)((now - base) / 1000);
}

void Sys_Sleep(int msec)
{
    if (msec > 0)
        c1p_sleep_us((unsigned)msec * 1000u);
}

/* --- paths --- */
static char binary_path[MAX_OSPATH], install_path[MAX_OSPATH] = "/base";

void Sys_SetBinaryPath(const char *path) { Q_strncpyz(binary_path, path, sizeof(binary_path)); }
char *Sys_BinaryPath(void) { return binary_path; }
void Sys_SetDefaultInstallPath(const char *path) { Q_strncpyz(install_path, path, sizeof(install_path)); }
char *Sys_DefaultInstallPath(void) { return install_path; }
char *Sys_DefaultAppPath(void) { return binary_path; }
char *Sys_DefaultHomeConfigPath(void) { return "/home"; }
char *Sys_DefaultHomeDataPath(void) { return "/home"; }
char *Sys_DefaultHomeStatePath(void) { return "/home"; }
char *Sys_SteamPath(void) { return ""; }
char *Sys_GogPath(void) { return ""; }
char *Sys_MicrosoftStorePath(void) { return ""; }
char *Sys_Cwd(void) { return "/"; }

const char *Sys_Basename(char *path)
{
    static char base[MAX_OSPATH];
    const char *s = strrchr(path, '/');

    Q_strncpyz(base, s ? s + 1 : path, sizeof(base));
    return base;
}

const char *Sys_Dirname(char *path)
{
    static char dir[MAX_OSPATH];
    char *s;

    Q_strncpyz(dir, path, sizeof(dir));
    s = strrchr(dir, '/');
    if (!s)
        return ".";
    if (s == dir)
        s[1] = 0;
    else
        *s = 0;
    return dir;
}

FILE *Sys_FOpen(const char *ospath, const char *mode)
{
    if (c1fs_find(ospath) < 0 && c1fs_isdir(ospath))
        return NULL;
    return fopen(ospath, mode);
}

qboolean Sys_Mkdir(const char *path)
{
    /* directories are implied by the files; under /home anything goes */
    return !strncmp(path, "/home", 5) || c1fs_isdir(path);
}

FILE *Sys_Mkfifo(const char *ospath)
{
    (void)ospath;
    return NULL;
}

/* --- listing --- */
#define MAX_FOUND_FILES 0x1000

static void list_filtered(const char *basedir, const char *subdirs, char *filter, char **list, int *numfiles)
{
    char search[MAX_OSPATH], sub[MAX_OSPATH], name[MAX_OSPATH];
    struct c1dir d;

    if (*numfiles >= MAX_FOUND_FILES - 1 || !basedir[0])
        return;
    if (subdirs[0])
        Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
    else
        Q_strncpyz(search, basedir, sizeof(search));
    if (c1fs_opendir(&d, search) < 0)
        return;
    while (c1fs_readdir(&d)) {
        if (d.is_dir) {
            if (subdirs[0])
                Com_sprintf(sub, sizeof(sub), "%s/%s", subdirs, d.name);
            else
                Q_strncpyz(sub, d.name, sizeof(sub));
            list_filtered(basedir, sub, filter, list, numfiles);
        }
        if (*numfiles >= MAX_FOUND_FILES - 1)
            break;
        Com_sprintf(name, sizeof(name), "%s/%s", subdirs, d.name);
        if (!Com_FilterPath(filter, name, qfalse))
            continue;
        list[(*numfiles)++] = CopyString(name);
    }
}

char **Sys_ListFiles(const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs)
{
    char *list[MAX_FOUND_FILES], **copy;
    qboolean dironly = wantsubs;
    struct c1dir d;
    int n = 0, i;
    size_t elen;

    *numfiles = 0;
    if (filter)
        list_filtered(directory, "", filter, list, &n);
    else {
        if (!directory[0])
            return NULL;
        if (!extension)
            extension = "";
        if (extension[0] == '/' && !extension[1]) {
            extension = "";
            dironly = qtrue;
        }
        elen = strlen(extension);
        if (c1fs_opendir(&d, directory) < 0)
            return NULL;
        while (c1fs_readdir(&d) && n < MAX_FOUND_FILES - 1) {
            const size_t len = strlen(d.name);

            if (dironly != (d.is_dir != 0))
                continue;
            if (elen && (len < elen || Q_stricmp(d.name + len - elen, extension)))
                continue;
            list[n++] = CopyString(d.name);
        }
    }
    if (!n)
        return NULL;
    copy = Z_Malloc((n + 1) * sizeof(*copy));
    for (i = 0; i < n; i++)
        copy[i] = list[i];
    copy[n] = NULL;
    *numfiles = n;
    return copy;
}

void Sys_FreeFileList(char **list)
{
    int i;

    if (!list)
        return;
    for (i = 0; list[i]; i++)
        Z_Free(list[i]);
    Z_Free(list);
}

/* --- the 68k between frames: its console commands --- */
void sys_c1_poll(void);
void sys_c1_poll(void)
{
    const char *cmd = c1p_poll();

    if (cmd) {
        Cbuf_AddText(cmd);
        Cbuf_AddText("\n");
    }
}

/* --- the start --- */
void c1q3_main(const char *cmdline)
{
    static char line[MAX_STRING_CHARS];

    Q_strncpyz(line, cmdline, sizeof(line));
    Sys_Print(va("ioquake3 " PRODUCT_VERSION " on the Z3660's core 1: %s\n", line));
    module_files();
    Sys_Milliseconds();
    Com_Init(line);
    NET_Init();
    for (;;) {
        Com_Frame();
        c1p_frame_done();
    }
}
