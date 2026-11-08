/*
 * core1_main.c -- z3660ARM_Core_uae: uae4arm on the Z3660's core 1, bare
 * metal (Xilinx standalone BSP for ps7_cortexa9_1, newlib).
 *
 * The harness is the one every core 1 program has (amix-gate
 * firmware/core1/cores/h264bench/arm_main.c: control block, MMU, faults,
 * -reload), cut down to what this image needs; the emulator's side is the
 * protocol in c1uae_board.h:
 *
 *   files      in at start-up through the window, held in memory (c1_syscalls.c)
 *   picture    RGB565 from the core -> 0x00RRGGBB into the RTG overlay's
 *              back buffer, flipped by core 0 at vblank (C1LD_W_FLIP)
 *   sound      48 kHz straight into the board's HDMI ring; the DMA's
 *              position paces the frames (the clock is the sound, as vplay)
 *   input      a ring the 68k writes, read before each frame
 *
 * No interrupts, no UART: core 0 owns both.  After STOP the image only
 * waits for a reload (the emulator does not start twice).
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <malloc.h>
#include <sys/time.h>
#include "xil_mmu.h"
#include "xil_cache.h"
#include "xil_cache_l.h"
#include "xil_io.h"
#include "xil_exception.h"
#include "xtime_l.h"
#include "bench.h"
#include "c1uae.h"
#include "c1uae_board.h"
#include "c1fs.h"

#define RTG_BASE            0x18000000UL
#define CORE0_ACK           (*(volatile uint32_t *)0xFFFF0000)
#define WT                  0b10
#define RTG_POLICY          (0x14DE2 | (WT << 12) | (WT << 2))
#define FAULT_POLICY        0
#define ARM_PLL_CTRL        0xF8000100
#define ARM_CLK_CTRL        0xF8000120
#define RELOAD_TRAMP        0x3FF00000UL
#define RELOAD_TTB          (RELOAD_TRAMP + 0x4000UL)

/* the image's first words: the mark core 0 and armbench check, the entry */
extern void _vector_table(void);
__attribute__((section(".benchid"), used))
static const uint32_t bench_id[2] = { 0x48323634, (uint32_t)_vector_table };

#define UAE_VERSION         HB_VERSION(0, 1, 0)     /* cores/CHANGES.md */
#define HB_PART_UAE         0x40                    /* the Amiga emulator */
static const char core_name[16] = "uae";

static volatile uint32_t *ctl = (volatile uint32_t *)(RTG_BASE + HB_CTL_OFF);
static volatile uint32_t *ua = (volatile uint32_t *)(RTG_BASE + C1UA_OFF);
static volatile uint8_t *board = (volatile uint8_t *)RTG_BASE;

static uint32_t be(uint32_t v) { return __builtin_bswap32(v); }
static void put(int w, uint32_t v) { ctl[w] = be(v); }
static uint32_t get(int w) { return be(ctl[w]); }
static void uput(int w, uint32_t v) { ua[w] = be(v); }
static uint32_t uget(int w) { return be(ua[w]); }

/* --- the heap: below the image from 0x30000000, then above it (arm_main.c) --- */
extern char _heap_start[], _heap_end[];
static char *brk_cur, *brk_end;

void *_sbrk(ptrdiff_t incr);
void *_sbrk(ptrdiff_t incr)
{
    char *p;

    if (!brk_cur) {
        brk_cur = (char *)0x30000000UL;
        brk_end = (char *)C1LD_LOAD_ADDR;
    }
    if (incr > brk_end - brk_cur) {
        if (brk_end != _heap_end && incr <= _heap_end - _heap_start) {
            brk_cur = _heap_start;
            brk_end = _heap_end;
        } else {
            errno = ENOMEM;
            return (void *)-1;
        }
    }
    p = brk_cur;
    brk_cur += incr;
    return p;
}

/* --- time: the global timer runs at half the ARM clock, read from the PLL --- */
static uint64_t tick_hz = COUNTS_PER_SECOND;

static uint64_t ticks(void)
{
    XTime t;

    XTime_GetTime(&t);
    return t;
}

static uint32_t cpu_hz(void)
{
    uint32_t fbdiv = (Xil_In32(ARM_PLL_CTRL) >> 12) & 0x7F;
    uint32_t div = (Xil_In32(ARM_CLK_CTRL) >> 8) & 0x3F;

    return div ? (uint32_t)(33333333ULL * fbdiv / div) : 0;
}

uint64_t c1p_now_us(void)
{
    return ticks() * 1000000ULL / tick_hz;
}

int _gettimeofday(struct timeval *tv, void *tz);
int _gettimeofday(struct timeval *tv, void *tz)
{
    const uint64_t us = c1p_now_us();

    (void)tz;
    if (tv) {
        tv->tv_sec = (time_t)(us / 1000000);
        tv->tv_usec = (suseconds_t)(us % 1000000);
    }
    return 0;
}

/* --- the log ring --- */
void c1p_log(const char *s)
{
    uint32_t head = uget(C1UA_W_LOG_HEAD);

    for (; *s; s++, head++)
        board[C1UA_OFF + C1UA_LOG_OFF + head % C1UA_LOG_LEN] = (uint8_t)*s;
    uput(C1UA_W_LOG_HEAD, head);
}

/* --- MMU without whole-L2 operations (core 0 does those during SCSI I/O) --- */
extern u32 MMUTable[];

static void set_section(uint32_t addr, uint32_t attr)
{
    MMUTable[addr >> 20] = attr ? (addr & 0xFFF00000UL) | attr : 0;
}

static void mmu_sync(void)
{
    Xil_L1DCacheFlush();
    __asm__ volatile("dsb\n mcr p15, 0, %0, c8, c7, 0\n mcr p15, 0, %0, c7, c5, 6\n dsb\n isb"
                     : : "r"(0) : "memory");
}

/* --- reload: a new image over this one (arm_main.c's trampoline) --- */
static inline uint32_t ttbr0(void)
{
    uint32_t v;

    __asm__ volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

extern void c1_reload_tramp(uint32_t dst, uint32_t src, uint32_t size, uint32_t entry);
extern char c1_reload_tramp_end[];
__asm__(
    ".text\n.arm\n.align 5\n.global c1_reload_tramp\n"
    "c1_reload_tramp:\n"
    "  mov r4, r0\n  mov r5, r1\n  mov r6, r2\n"
    "1: ldr r7, [r1], #4\n  str r7, [r0], #4\n  subs r2, r2, #4\n  bgt 1b\n"
    "  dsb\n  mov r8, #0\n"
    "2: mov r9, #0\n"
    "3: orr r10, r9, r8\n  mcr p15, 0, r10, c7, c14, 2\n"
    "  add r9, r9, #32\n  cmp r9, #0x2000\n  blt 3b\n"
    "  adds r8, r8, #0x40000000\n  bne 2b\n"
    "  dsb\n  mrc p15, 0, r10, c1, c0, 0\n"
    "  bic r10, r10, #0x1\n  bic r10, r10, #0x4\n  bic r10, r10, #0x1000\n"
    "  mcr p15, 0, r10, c1, c0, 0\n  isb\n"
    "  mov r0, r4\n  mov r1, r5\n  mov r2, r6\n"
    "4: ldr r7, [r1], #4\n  str r7, [r0], #4\n  subs r2, r2, #4\n  bgt 4b\n"
    "  dsb\n  mov r10, #0\n"
    "  mcr p15, 0, r10, c7, c5, 0\n  mcr p15, 0, r10, c7, c5, 6\n  mcr p15, 0, r10, c8, c7, 0\n"
    "  dsb\n  isb\n  bx r3\n"
    ".global c1_reload_tramp_end\nc1_reload_tramp_end:\n");

static int reload_posted(void)
{
    const uint32_t src = get(W_DATA_OFF), size = get(W_DATA_SIZE), entry = get(W_FLAGS);
    const volatile uint32_t *img = (const volatile uint32_t *)(RTG_BASE + src);
    uint32_t n = (uint32_t)(c1_reload_tramp_end - (char *)c1_reload_tramp);
    void (*tramp)(uint32_t, uint32_t, uint32_t, uint32_t) =
        (void (*)(uint32_t, uint32_t, uint32_t, uint32_t))RELOAD_TRAMP;

    if (get(W_STATE) != HB_RELOAD)
        return 0;
    if (!(src == HB_DATA_OFF && size >= 8 && size <= HB_IMAGE_MAX && img[0] == 0x48323634 &&
          entry >= C1LD_LOAD_ADDR && entry < C1LD_LOAD_ADDR + size && !(entry & 3) &&
          C1LD_LOAD_ADDR + size <= RELOAD_TRAMP)) {
        put(W_ERRCODE, (uint32_t)-10);
        put(W_STATE, HB_ERROR);
        return 1;
    }
    put(W_STATE, HB_IDLE);
    put(W_MAGIC, 0);
    put(W_STAGE, 0);
    uput(C1UA_W_MAGIC, 0);
    memcpy((void *)RELOAD_TRAMP, (const void *)c1_reload_tramp, n);
    memcpy((void *)RELOAD_TTB, (const void *)(ttbr0() & ~0x3FFFUL), 0x4000);
    __asm__ volatile("dsb\n mcr p15, 0, %0, c2, c0, 0\n isb\n mcr p15, 0, %1, c8, c7, 0\n dsb\n isb"
                     : : "r"(RELOAD_TTB), "r"(0) : "memory");
    __asm__ volatile("dsb\n mcr p15, 0, %0, c7, c5, 0\n mcr p15, 0, %0, c7, c5, 6\n isb"
                     : : "r"(0) : "memory");
    tramp(C1LD_LOAD_ADDR, RTG_BASE + src, (size + 3) & ~3UL, entry);
    return 1;   /* not reached */
}

/* --- faults: say so, then wait for a reload --- */
extern u32 DataAbortAddr, PrefetchAbortAddr, UndefinedExceptionAddr;

static void fault(void *kind)
{
    uint32_t far = 0, pc;

    switch ((uint32_t)kind) {
    case XIL_EXCEPTION_ID_DATA_ABORT_INT:
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(far));
        pc = DataAbortAddr;
        break;
    case XIL_EXCEPTION_ID_PREFETCH_ABORT_INT:
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(far));
        pc = PrefetchAbortAddr;
        break;
    default:
        pc = UndefinedExceptionAddr;
    }
    put(W_FAULT_ADDR, far);
    put(W_FAULT_PC, pc);
    put(W_FAULT, (uint32_t)kind);
    uput(C1UA_W_STATE, C1UA_FAILED);
    uput(C1UA_W_ERR, C1UA_E_CORE);
    put(W_STATE, HB_ERROR);
    for (;;)
        if (get(W_STATE) == HB_RELOAD) {
            put(W_FAULT, 0);
            reload_posted();
        }
}

/* --- load per second, as ztop reads it --- */
static uint64_t win_start, win_idle;

static void load_window(uint64_t now)
{
    const uint64_t span = now - win_start;

    if (span < tick_hz)
        return;
    put(W_LOAD, (uint32_t)(1000 - (win_idle > span ? span : win_idle) * 1000 / span));
    put(W_HEARTBEAT, get(W_HEARTBEAT) + 1);
    put(W_NOW, (uint32_t)now);
    win_start = now;
    win_idle = 0;
}

/* --- the picture --- */
static struct { int dbuf; } ovl;

void c1p_video(const uint16_t *pix, unsigned w, unsigned h, size_t pitch)
{
    volatile uint32_t *c1ld = (volatile uint32_t *)(board + C1LD_OFF);
    const uint32_t opitch = uget(C1UA_W_OVL_PITCH), row0 = uget(C1UA_W_OVL_ROW0);
    const uint32_t maxw = uget(C1UA_W_OVL_MAXW), maxh = uget(C1UA_W_OVL_MAXH);
    const unsigned sc = uget(C1UA_W_OVL_SCALE) == 2 ? 2 : 1;
    static uint32_t line[2048];
    uint32_t back;
    volatile uint8_t *dst;
    unsigned x, y, ow, oh;

    if (!opitch)
        return;                         /* no window yet, or a bench without picture */
    ovl.dbuf = (be(c1ld[C1LD_W_FEAT]) & C1LD_FEAT_OVL_FLIP) != 0;
    if (ovl.dbuf) {                     /* the last flip not taken yet: wait (at most 40 ms) */
        const uint64_t t0 = c1p_now_us();

        while (c1ld[C1LD_W_FLIP] != 0 && c1p_now_us() - t0 < 40000)
            ;
    }
    ow = w * sc;
    oh = h * sc;
    if (ow > maxw)
        ow = maxw;
    if (oh > maxh)
        oh = maxh;
    if (ow > 2048 || ow * 4 > opitch)
        return;
    back = !ovl.dbuf || be(c1ld[C1LD_W_FRONT]) == 2 ? C1UA_OVL0_OFF : C1UA_OVL1_OFF;
    dst = board + back + row0 * opitch;
    for (y = 0; y < oh; y++) {
        const uint16_t *src = (const uint16_t *)((const uint8_t *)pix + (y / sc) * pitch);

        if (sc == 1 || !(y & 1)) {
            for (x = 0; x < ow; x++) {
                const uint32_t p = src[x / sc];
                const uint32_t q = ((p >> 11) << 19 | (p >> 13) << 16) |
                                   (((p >> 5) & 63) << 10 | ((p >> 9) & 3) << 8) |
                                   ((p & 31) << 3 | ((p >> 2) & 7));

                line[x] = q ? q : 0x010101;     /* 0 is transparent in the overlay */
            }
        }
        memcpy((void *)(dst + y * opitch), line, ow * 4);
    }
    if (ovl.dbuf)
        c1ld[C1LD_W_FLIP] = be(back == C1UA_OVL0_OFF ? 1 : 2);
}

/* --- the sound: into the HDMI ring ahead of its DMA --- */
#define SND_PERIOD  3840u               /* bytes, 20 ms: the firmware's period */
#define SND_LEAD    (SND_PERIOD * 3)    /* what we keep written ahead of the DMA */
static struct {
    uint32_t last;
    uint64_t dma, wr;
    int live;                           /* the DMA moves */
    uint32_t volume;
} snd;

static void dma_update(void)
{
    const uint32_t pos = Xil_In32(C1UA_HDMI_POS_REG) & ~3u;

    if (pos >= C1UA_HDMI_SIZE)
        return;
    if (pos != snd.last)
        snd.live = 1;
    snd.dma += (pos + C1UA_HDMI_SIZE - snd.last) % C1UA_HDMI_SIZE;
    snd.last = pos;
}

void c1p_audio(const int16_t *lr, size_t frames)
{
    volatile uint32_t *ring = (volatile uint32_t *)(board + C1UA_HDMI_OFF);
    size_t i;

    dma_update();
    if (snd.wr < snd.dma + 4) {         /* ran dry: start again a period ahead */
        if (snd.live)
            uput(C1UA_W_UNDERRUNS, uget(C1UA_W_UNDERRUNS) + 1);
        snd.wr = snd.dma + SND_PERIOD;
    }
    for (i = 0; i < frames; i++) {
        const int32_t l = lr[i * 2] * (int32_t)snd.volume >> 8, r = lr[i * 2 + 1] * (int32_t)snd.volume >> 8;

        ring[(uint32_t)(snd.wr / 4 % (C1UA_HDMI_SIZE / 4))] = (uint32_t)(uint16_t)l | (uint32_t)(uint16_t)r << 16;
        snd.wr += 4;
    }
}

/* --- input from the ring --- */
void c1p_input(void)
{
    uint32_t tail = uget(C1UA_W_IN_TAIL);
    const uint32_t head = uget(C1UA_W_IN_HEAD);
    const volatile uint32_t *ev = (const volatile uint32_t *)(board + C1UA_OFF + C1UA_IN_OFF);

    while (tail != head) {
        const uint32_t e = be(ev[tail % C1UA_IN_N]);
        const uint32_t d = e & 0xFFFFFF;

        switch (e >> 24) {
        case C1UA_EV_KEY:
            if ((d & 0xFFFF) < 512)
                c1_key[d & 0xFFFF] = (d >> 16) & 1;
            break;
        case C1UA_EV_MOUSE:
            c1_mouse_dx += (int16_t)((int32_t)(d << 8) >> 20);
            c1_mouse_dy += (int16_t)((int32_t)(d << 20) >> 20);
            break;
        case C1UA_EV_BUTTONS:
            c1_mouse_btn = (uint8_t)d;
            break;
        case C1UA_EV_JOY:
            c1_joy[(d >> 16) & 1] = (uint16_t)d;
            break;
        }
        tail++;
    }
    uput(C1UA_W_IN_TAIL, tail);
}

void c1p_idle(void)
{
}

uint32_t c1_dbg_pc(void);
uint32_t c1_dbg_chip_long(uint32_t addr);
void c1_dbg_chip_str(uint32_t addr, char *buf, unsigned n);

/* --- commands while idle: files in --- */
static void chunk(void)
{
    const uint32_t f = uget(C1UA_W_CHUNK_FILE), off = uget(C1UA_W_CHUNK_OFF), len = uget(C1UA_W_CHUNK_LEN);
    const volatile uint32_t *fw = ua + C1UA_W_FILES + f * 3;

    if (f >= C1UA_MAX_FILES || len > C1UA_WIN_SIZE) {
        uput(C1UA_W_RESULT, (uint32_t)-1);
        return;
    }
    if (!c1fs[f].data) {                /* the first chunk makes the file */
        char name[C1UA_NAME_LEN + 1];
        int i;

        for (i = 0; i < C1UA_NAME_LEN; i++)
            name[i] = (char)board[C1UA_OFF + C1UA_NAMES_OFF + f * C1UA_NAME_LEN + i];
        name[C1UA_NAME_LEN] = 0;
        if (c1fs_add(name, be(fw[0]), (int)(be(fw[1]) & C1UA_FILE_RW)) != (int)f) {
            uput(C1UA_W_RESULT, (uint32_t)-2);
            uput(C1UA_W_ERR, C1UA_E_NOMEM);
            return;
        }
    }
    if ((size_t)off + len > c1fs[f].size) {
        uput(C1UA_W_RESULT, (uint32_t)-3);
        return;
    }
    memcpy(c1fs[f].data + off, (const void *)(board + C1UA_WIN_OFF), len);
    if (be(fw[1]) & C1UA_FILE_SWAB) {   /* an EPROM image: bytes of each word swapped */
        uint32_t i;

        for (i = off & ~1u; i + 1 < off + len; i += 2) {
            const uint8_t t = c1fs[f].data[i];

            c1fs[f].data[i] = c1fs[f].data[i + 1];
            c1fs[f].data[i + 1] = t;
        }
    }
    uput(C1UA_W_RESULT, 0);
}

/* the config text into the frontend; returns the content's name */
static const char *config(void)
{
    static char text[C1UA_TEXT_LEN + 1], content[C1UA_NAME_LEN + 1];
    char *line, *save;
    int i;

    for (i = 0; i < C1UA_TEXT_LEN; i++)
        text[i] = (char)board[C1UA_OFF + C1UA_TEXT_OFF + i];
    text[C1UA_TEXT_LEN] = 0;
    content[0] = 0;
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "model=", 6))
            c1uae_set_var("uae4arm_model", line + 6);
        else if (!strncmp(line, "var=", 4)) {
            char *eq = strchr(line + 4, '=');

            if (eq) {
                *eq = 0;
                c1uae_set_var(line + 4, eq + 1);
            }
        } else if (!strncmp(line, "cfg=", 4))
            c1uae_add_line(line + 4);
        else if (!strncmp(line, "content=", 8))
            strncpy(content, line + 8, C1UA_NAME_LEN);
    }
    return content[0] ? content : NULL;
}

/* the next changed block of a writable file into the window */
static void flush_one(void)
{
    int f;
    size_t off, len;

    if (c1fs_next_dirty(&f, &off, &len) == 0) {
        memcpy((void *)(board + C1UA_WIN_OFF), c1fs[f].data + off, len);
        uput(C1UA_W_FLUSH_FILE, (uint32_t)f);
        uput(C1UA_W_FLUSH_OFF, (uint32_t)off);
        uput(C1UA_W_FLUSH_LEN, (uint32_t)len);
        uput(C1UA_W_RESULT, 0);
    } else
        uput(C1UA_W_RESULT, (uint32_t)-1);
    uput(C1UA_W_DIRTY, c1fs_dirty_count());
}

/* --- while the Amiga runs: the 68k's commands between frames --- */
static int running_commands(int *paused)
{
    const uint32_t cmd = uget(C1UA_W_CMD);

    switch (cmd) {
    case C1UA_CMD_NONE:
        return 0;
    case C1UA_CMD_STOP:
        uput(C1UA_W_CMD, 0);
        return 1;
    case C1UA_CMD_FLUSH:
        flush_one();
        break;
    case C1UA_CMD_RESET: {
        extern void retro_reset(void);

        retro_reset();
        break;
    }
    case C1UA_CMD_PAUSE:
        *paused = uget(C1UA_W_ARG) != 0;
        break;
    }
    uput(C1UA_W_CMD, 0);
    return 0;
}

static void run(int bench)
{
    const char *content = config();
    uint64_t t0, t1, avg = 0, bench_t0 = 0;
    const uint32_t bench_frames = bench ? uget(C1UA_W_ARG) : 0;
    int paused = 0;

    uput(C1UA_W_FRAMES, 0);
    uput(C1UA_W_SKIPPED, 0);
    uput(C1UA_W_UNDERRUNS, 0);
    snd.volume = uget(C1UA_W_VOLUME) ? uget(C1UA_W_VOLUME) : 256;
    snd.last = Xil_In32(C1UA_HDMI_POS_REG) & ~3u;
    if (snd.last >= C1UA_HDMI_SIZE)
        snd.last = 0;
    snd.dma = 0;
    snd.wr = SND_PERIOD;
    memset((void *)(board + C1UA_HDMI_OFF), 0, C1UA_HDMI_SIZE);
    uput(C1UA_W_STATE, C1UA_RUNNING);
    uput(C1UA_W_CMD, 0);
    if (c1uae_start("/", content ? content : "") < 0) {
        uput(C1UA_W_ERR, C1UA_E_CONFIG);
        uput(C1UA_W_STATE, C1UA_FAILED);
        return;
    }
    put(W_STATE, HB_RUNNING);
    for (;;) {
        if (running_commands(&paused))
            break;
        if (paused) {
            const uint64_t w = ticks();

            while (ticks() - w < tick_hz / 100)
                ;
            win_idle += ticks() - w;
            load_window(ticks());
            continue;
        }
        if (bench && c1_frames == 1)
            bench_t0 = ticks();
        t0 = ticks();
        c1uae_frame();
        t1 = ticks();
        {
            const uint32_t us = (uint32_t)((t1 - t0) * 1000000ULL / tick_hz);

            avg = avg ? (avg * 49 + us) / 50 : us;
            uput(C1UA_W_FRAME_US, us);
            uput(C1UA_W_FRAME_US_AVG, (uint32_t)avg);
            uput(C1UA_W_FRAMES, c1_frames);
            uput(C1UA_W_PC, c1_dbg_pc());
            put(W_PROGRESS, c1_frames);
        }
        if (bench) {
            static uint64_t bm_t0;
            static uint32_t bm_f0;
            const uint32_t m = c1_dbg_chip_long(C1BENCH_MARK);

            if ((m >> 8) == 0x433142 && (m & 0xFF) != uget(C1UA_W_BM_STATE)) {
                uput(C1UA_W_BM_STATE, m & 0xFF);
                if ((m & 0xFF) == 'S') {
                    bm_t0 = t1;
                    bm_f0 = c1_frames;
                } else if ((m & 0xFF) == 'D') {
                    static char text[1024];
                    const uint64_t t = t1 - bm_t0;

                    uput(C1UA_W_BM_FRAMES, c1_frames - bm_f0);
                    uput(C1UA_W_BM_TICKS_LO, (uint32_t)t);
                    uput(C1UA_W_BM_TICKS_HI, (uint32_t)(t >> 32));
                    c1_dbg_chip_str(C1BENCH_TEXT, text, sizeof(text));
                    c1p_log(text);
                }
            }
            if (c1_frames > bench_frames || uget(C1UA_W_BM_STATE) == 'D') {
                const uint64_t t = ticks() - bench_t0;

                uput(C1UA_W_BENCH_TICKS_LO, (uint32_t)t);
                uput(C1UA_W_BENCH_TICKS_HI, (uint32_t)(t >> 32));
                break;
            }
            load_window(ticks());
            continue;
        }
        /* pacing: the sound's DMA, or the timer at 50 Hz without it */
        dma_update();
        if (snd.live) {
            const uint64_t w = ticks();

            while (snd.wr > snd.dma + SND_LEAD && ticks() - w < tick_hz / 10)
                dma_update();
            win_idle += ticks() - w;
        } else {
            static uint64_t next;
            const uint64_t w = ticks();

            if (!next || w > next + tick_hz / 10)
                next = w;
            next += tick_hz / 50;
            while (ticks() < next)
                ;
            win_idle += ticks() - w;
        }
        load_window(ticks());
    }
    uput(C1UA_W_DIRTY, c1fs_dirty_count());
    uput(C1UA_W_STATE, C1UA_STOPPED);
    put(W_STATE, HB_DONE);
}

int main(void)
{
    int i;

    set_section(RTG_BASE + HB_CTL_OFF, NORM_NONCACHE);
    mmu_sync();
    put(W_STAGE, 1);
    put(W_FAULT, 0);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_DATA_ABORT_INT, fault, (void *)XIL_EXCEPTION_ID_DATA_ABORT_INT);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_PREFETCH_ABORT_INT, fault,
                                 (void *)XIL_EXCEPTION_ID_PREFETCH_ABORT_INT);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_UNDEFINED_INT, fault, (void *)XIL_EXCEPTION_ID_UNDEFINED_INT);
    put(W_STAGE, 2);
    Xil_ICacheEnable();
    Xil_L1DCacheEnable();
    put(W_STAGE, 3);
    for (i = 0; i < 0x180; i++)                 /* the 060's RAM and below: faults */
        set_section(i * 0x100000UL, FAULT_POLICY);
    for (i = 0x200; i < 0x300; i++)             /* core 0's firmware */
        set_section(i * 0x100000UL, FAULT_POLICY);
    for (i = 0x182; i < 0x1FE; i++)             /* board memory, as the emulator maps it */
        set_section(i * 0x100000UL, RTG_POLICY);
    set_section(RTG_BASE + HB_CTL_OFF, NORM_NONCACHE);     /* also C1UA_OFF and C1LD_OFF */
    for (i = 0; i < (int)(HB_IMAGE_MAX >> 20); i++)
        set_section(RTG_BASE + HB_DATA_OFF + i * 0x100000UL, NORM_NONCACHE);
    for (i = 0; i < (int)(C1UA_WIN_SIZE >> 20); i++)
        set_section(RTG_BASE + C1UA_WIN_OFF + i * 0x100000UL, NORM_NONCACHE);
    set_section(RTG_BASE + C1UA_HDMI_OFF, NORM_NONCACHE);  /* the DMA reads DDR */
    set_section(RELOAD_TRAMP, NORM_NONCACHE);
    set_section(0xFFF00000UL, NORM_NONCACHE);
    mmu_sync();
    put(W_STAGE, 4);

    for (i = 0; i < 4; i++)
        put(W_NAME + i, (uint32_t)core_name[i * 4] << 24 | (uint32_t)core_name[i * 4 + 1] << 16 |
                        (uint32_t)core_name[i * 4 + 2] << 8 | (uint32_t)core_name[i * 4 + 3]);
    put(W_VERSION, UAE_VERSION);
    put(W_PARTS, HB_PART_UAE);
    if (cpu_hz())
        tick_hz = cpu_hz() / 2;
    put(W_CPU_HZ, cpu_hz());
    put(W_TICK_HZ, (uint32_t)tick_hz);
    for (i = 0; i < C1UA_W_NWORDS; i++)
        ua[i] = 0;
    uput(C1UA_W_VERSION, C1UA_VERSION);
    uput(C1UA_W_CPU_HZ, cpu_hz());
    uput(C1UA_W_TICK_HZ, (uint32_t)tick_hz);
    uput(C1UA_W_STATE, C1UA_IDLE);
    uput(C1UA_W_MAGIC, C1UA_MAGIC);
    put(W_STATE, HB_IDLE);
    put(W_LOAD, 0);
    win_start = ticks();
    put(W_MAGIC, HB_MAGIC_READY);
    put(W_STAGE, 5);
    CORE0_ACK = 0;

    for (;;) {
        const uint32_t cmd = uget(C1UA_W_CMD);

        if (reload_posted())
            continue;
        if (cmd == C1UA_CMD_CHUNK) {
            uput(C1UA_W_STATE, C1UA_LOADING);
            chunk();
            uput(C1UA_W_CMD, 0);
        } else if ((cmd == C1UA_CMD_START || cmd == C1UA_CMD_BENCH) && uget(C1UA_W_STATE) != C1UA_STOPPED &&
                   uget(C1UA_W_STATE) != C1UA_RUNNING) {
            run(cmd == C1UA_CMD_BENCH);
            uput(C1UA_W_CMD, 0);
        } else if (cmd == C1UA_CMD_FLUSH) {    /* after STOP too: the last changes */
            flush_one();
            uput(C1UA_W_CMD, 0);
        } else if (cmd != C1UA_CMD_NONE) {
            uput(C1UA_W_RESULT, (uint32_t)-9);     /* not now: STOPPED needs a reload */
            uput(C1UA_W_CMD, 0);
        } else {
            const uint64_t t0 = ticks();

            while (ticks() - t0 < tick_hz / 1000)
                __asm__ volatile("nop");
            win_idle += ticks() - t0;
            load_window(ticks());
        }
    }
}
