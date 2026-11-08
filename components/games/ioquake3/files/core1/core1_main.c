/*
 * core1_main.c -- z3660ARM_Core_q3: ioquake3 on the Z3660's core 1, bare
 * metal (Xilinx standalone BSP for ps7_cortexa9_1, newlib).
 *
 * The harness every core 1 program has (amix-gate firmware/core1
 * cores/h264bench/arm_main.c: control block, MMU, faults, -reload), as
 * uae4arm's core1_main.c, with the protocol of c1q3_board.h:
 *
 *   files      in at start-up through the window, held in memory (c1fs.c);
 *              what the engine writes under /home goes back (CMD_FLUSH)
 *   picture    OSMesa's RGBA frame -> 0x00RRGGBB into the RTG overlay's
 *              back buffer, flipped by core 0 at vblank (C1LD_W_FLIP)
 *   sound      the engine mixes into its own ring; the private timer's
 *              interrupt (200 Hz) carries it on into the board's HDMI ring
 *              ahead of the DMA, so a slow frame does not starve it
 *   input      a ring the 68k writes, read at every frame
 *
 * No UART: core 0 owns it.  After the game quits the image only waits for
 * a reload (the engine does not start twice).
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
#include "xil_io.h"
#include "xil_exception.h"
#include "xtime_l.h"
#include "bench.h"
#include "c1q3.h"
#include "c1q3_board.h"
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

#define Q3_VERSION          HB_VERSION(0, 1, 0)     /* cores/CHANGES.md */
static const char core_name[16] = "q3";

static volatile uint32_t *ctl = (volatile uint32_t *)(RTG_BASE + HB_CTL_OFF);
static volatile uint32_t *pg = (volatile uint32_t *)(RTG_BASE + C1Q3_OFF);
static volatile uint8_t *board = (volatile uint8_t *)RTG_BASE;

static uint32_t be(uint32_t v) { return __builtin_bswap32(v); }
static void put(int w, uint32_t v) { ctl[w] = be(v); }
static uint32_t get(int w) { return be(ctl[w]); }
static void qput(int w, uint32_t v) { pg[w] = be(v); }
static uint32_t qget(int w) { return be(pg[w]); }

/*
 * --- the heap: 0x30000000 up to the image (128 MB), and above the image's
 * .bss to the stack.  dlmalloc 2.8.6 (c1_malloc.c, built with a
 * non-contiguous MORECORE) takes its memory from c1p_morecore, which hands
 * out the region a request fits in, the current one first; dlmalloc keeps
 * the two as separate segments, so neither region's rest is lost.  Big
 * files that stay for the whole run (the paks) are cut from the top of the
 * high region (c1p_file_alloc), before the engine starts.
 */
extern char _heap_start[], _heap_end[];
static struct { char *cur, *end, *start; } region[2];
static int cur_region = -1;
static void heap_init(void);

void *c1p_morecore(ptrdiff_t incr);
void *c1p_morecore(ptrdiff_t incr)
{
    char *p;
    int r;

    heap_init();
    r = cur_region;
    if (incr < 0)
        return (void *)-1;              /* MORECORE_CANNOT_TRIM */
    if (incr > region[r].end - region[r].cur) {
        r ^= 1;
        if (incr > region[r].end - region[r].cur)
            return (void *)-1;
        cur_region = r;
    }
    p = region[r].cur;
    region[r].cur += incr;
    return p;
}

static void heap_init(void)
{
    if (cur_region < 0) {
        region[0].start = region[0].cur = (char *)0x30000000UL;
        region[0].end = (char *)C1LD_LOAD_ADDR;
        region[1].start = region[1].cur = _heap_start;
        region[1].end = _heap_end;
        cur_region = 0;
    }
}

void *c1p_file_alloc(size_t size)
{
    heap_init();
    size = (size + 63) & ~(size_t)63;
    if (size <= (size_t)(region[1].end - region[1].cur)) {
        region[1].end -= size;
        return region[1].end;
    }
    return NULL;
}

static uint32_t heap_left_kb(void)
{
    heap_init();
    return (uint32_t)(((size_t)(region[0].end - region[0].cur) + (size_t)(region[1].end - region[1].cur)) >> 10);
}

/* the gpu core's executor in libOSMesa.a (gallium zz3660) asks for these;
   this image draws with OSMesa straight, so they only answer */
size_t hb_heap_free(void);
size_t hb_heap_free(void)
{
    return (size_t)heap_left_kb() * 1024 + (size_t)mallinfo().fordblks;
}

void zzp_refused(uint32_t what, uint32_t size);
void zzp_refused(uint32_t what, uint32_t size)
{
    (void)what; (void)size;
}

void zzp_present(const void *rgba, int w, int h, int again);
void zzp_present(const void *rgba, int w, int h, int again)
{
    (void)rgba; (void)w; (void)h; (void)again;
}

/* --- time: the global timer runs at half the ARM clock, read from the PLL --- */
static uint64_t tick_hz = COUNTS_PER_SECOND;

static uint64_t ticks(void)
{
    volatile uint32_t *gt = (volatile uint32_t *)0xF8F00200UL;
    uint32_t hi, lo;

    do {
        hi = gt[1];
        lo = gt[0];
    } while (gt[1] != hi);
    return (uint64_t)hi << 32 | lo;
}

static uint32_t cpu_hz(void)
{
    uint32_t fbdiv = (Xil_In32(ARM_PLL_CTRL) >> 12) & 0x7F;
    uint32_t div = (Xil_In32(ARM_CLK_CTRL) >> 8) & 0x3F;

    return div ? (uint32_t)(33333333ULL * fbdiv / div) : 0;
}

uint64_t c1p_now_us(void)
{
    const uint64_t t = ticks();     /* t * 1000000 would wrap after 11.8 hours up */

    return t / tick_hz * 1000000ULL + t % tick_hz * 1000000ULL / tick_hz;
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

/* for Mesa (armgl_compat.c) */
unsigned long long armgl_now_us(void);
unsigned long long armgl_now_us(void)
{
    return c1p_now_us();
}

/* --- the log ring, at the window's end --- */
void c1p_log(const char *s)
{
    uint32_t head = qget(C1Q3_W_LOG_HEAD);

    for (; *s; s++, head++)
        board[C1Q3_LOG_OFF + head % C1Q3_LOG_LEN] = (uint8_t)*s;
    qput(C1Q3_W_LOG_HEAD, head);
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

static void samp_stop(void);

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
    samp_stop();                /* no interrupt into the image being overwritten */
    put(W_STATE, HB_IDLE);
    put(W_MAGIC, 0);
    put(W_STAGE, 0);
    qput(C1Q3_W_MAGIC, 0);
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

    samp_stop();                /* the reload below comes from this loop */
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
    qput(C1Q3_W_STATE, C1Q3_FAILED);
    qput(C1Q3_W_ERR, C1Q3_E_CORE);
    put(W_STATE, HB_ERROR);
    for (;;)
        if (get(W_STATE) == HB_RELOAD) {
            put(W_FAULT, 0);
            reload_posted();
        }
}

/*
 * --- the sound: the engine's ring -> the HDMI ring, from the timer interrupt ---
 *
 * HDMI frames are counted from the DMA's position (or, while the DMA does
 * not move, from the clock: qemu, a stopped formatter).  The engine's frame
 * f is HDMI frame f + off.  When the engine has not asked for the time for
 * longer than half its ring (a long load), off moves on, so its clock never
 * jumps further than its ring holds and it never paints behind the DMA.
 */
#define HDMI_FRAMES     (C1Q3_HDMI_SIZE / 4)    /* 7680: 160 ms */
#define SND_LEAD        3840u                   /* frames kept written ahead: 80 ms */
#define SND_RESTART     480u                    /* after running dry: 10 ms ahead */
static struct {
    int16_t *ring;                              /* the engine's, NULL: none */
    uint32_t mask;
    volatile int *painted;
    uint32_t last_pos;                          /* the DMA's byte position at the last look */
    uint32_t dma;                               /* HDMI frames played */
    uint64_t clock_t0;                          /* while the DMA stands: ticks at dma's last step */
    int live;
    uint32_t wr;                                /* the next HDMI frame we write */
    volatile uint32_t off;                      /* HDMI frame = engine frame + off */
    uint32_t last_now;                          /* what c1p_snd_now() said last */
    uint32_t volume;
} snd;

static void snd_dma_update(void)
{
    const uint32_t pos = Xil_In32(C1Q3_HDMI_POS_REG) & ~3u;

    if (pos < C1Q3_HDMI_SIZE && pos != snd.last_pos) {
        snd.dma += ((pos + C1Q3_HDMI_SIZE - snd.last_pos) % C1Q3_HDMI_SIZE) / 4;
        snd.last_pos = pos;
        snd.live = 1;
        return;
    }
    if (!snd.live) {                            /* no DMA: the clock says how far */
        const uint64_t t = ticks();
        const uint64_t step = tick_hz / C1Q3_HDMI_RATE;
        const uint32_t n = (uint32_t)((t - snd.clock_t0) / step);

        snd.dma += n;
        snd.clock_t0 += (uint64_t)n * step;
    }
}

static void snd_pump(void)
{
    volatile uint32_t *hdmi = (volatile uint32_t *)(board + C1Q3_HDMI_OFF);
    uint32_t end;

    snd_dma_update();
    if (!snd.ring)
        return;
    if ((int32_t)(snd.wr - snd.dma) < 0) {      /* ran dry */
        if (snd.live)
            qput(C1Q3_W_UNDERRUNS, qget(C1Q3_W_UNDERRUNS) + 1);
        snd.wr = snd.dma + SND_RESTART;
    }
    end = snd.dma + SND_LEAD;
    for (; (int32_t)(end - snd.wr) > 0; snd.wr++) {
        const uint32_t f = snd.wr - snd.off;    /* the engine's frame */
        uint32_t v = 0;

        if ((int32_t)(f - (uint32_t)*snd.painted) < 0) {
            const int16_t *s = snd.ring + (f & snd.mask) * 2;
            const int32_t l = s[0] * (int32_t)snd.volume >> 8, r = s[1] * (int32_t)snd.volume >> 8;

            v = (uint32_t)(uint16_t)l | (uint32_t)(uint16_t)r << 16;
        }
        hdmi[snd.wr % HDMI_FRAMES] = v;
    }
}

void c1p_snd_start(int16_t *ring, uint32_t frames, volatile int *painted)
{
    __asm__ volatile("cpsid i" ::: "memory");
    snd.mask = frames - 1;
    snd.painted = painted;
    snd.off = snd.dma;
    snd.last_now = 0;
    snd.wr = snd.dma + SND_RESTART;
    snd.ring = ring;
    __asm__ volatile("cpsie i" ::: "memory");
}

void c1p_snd_stop(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    snd.ring = NULL;
    memset((void *)(board + C1Q3_HDMI_OFF), 0, C1Q3_HDMI_SIZE);
    __asm__ volatile("cpsie i" ::: "memory");
}

uint32_t c1p_snd_now(void)
{
    uint32_t now;

    __asm__ volatile("cpsid i" ::: "memory");
    snd_dma_update();
    now = snd.dma - snd.off;
    if (now - snd.last_now > (snd.mask + 1) / 2) {  /* the engine slept long: catch up */
        snd.off = snd.dma - snd.last_now - SND_RESTART;
        now = snd.last_now + SND_RESTART;
    }
    snd.last_now = now;
    __asm__ volatile("cpsie i" ::: "memory");
    return now;
}

/*
 * --- the private timer, 200 Hz: the sound pump, and 4 times a second where
 * core 1 is (as cores/gpu/gpu.c): the interrupted pc, lr, sp and the code
 * addresses on the stack into C1Q3_W_SAMPLE..; it takes armbench -reload
 * -force when the engine spins without faulting.  Own vectors, only the
 * IRQ differs; it saves the caller-saved VFP/NEON registers, since the
 * engine and Mesa use them everywhere.
 */
#define PRIV(off)       (*(volatile uint32_t *)(0xF8F00000UL + (off)))
#define ICCICR          PRIV(0x100)
#define ICCPMR          PRIV(0x104)
#define ICCIAR          PRIV(0x10C)
#define ICCEOIR         PRIV(0x110)
#define PT_LOAD         PRIV(0x600)
#define PT_CTRL         PRIV(0x608)
#define PT_ISR          PRIV(0x60C)
#define ICDDCR          PRIV(0x1000)
#define ICDISER0        PRIV(0x1100)
#define ICDICER0        PRIV(0x1180)
#define ICDIPR_29       (*(volatile uint8_t *)0xF8F0141DUL)
#define PT_IRQ          29
#define PT_PRESCALE     15              /* half the CPU clock / 16 */
#define PT_HZ           200
#define PT_SAMPLE_EVERY 50              /* 4 Hz */
#define PT_ON           (PT_PRESCALE << 8 | 1 << 2 | 1 << 1 | 1)   /* interrupt, reload, on */

extern char __rodata_start[], _stack[];
extern void samp_vectors(void);

__asm__(".text\n"
        ".arm\n"
        ".balign 32\n"
        ".global samp_vectors\n"
        "samp_vectors:\n"
        "  b _vector_table+0\n"
        "  b _vector_table+4\n"
        "  b _vector_table+8\n"
        "  b _vector_table+12\n"
        "  b _vector_table+16\n"
        "  b _vector_table+20\n"
        "  b samp_irq\n"
        "  b _vector_table+28\n"
        "samp_irq:\n"
        "  sub lr, lr, #4\n"
        "  push {r0-r3, r12, lr}\n"
        "  vmrs r2, fpscr\n"
        "  push {r2, r3}\n"
        "  vpush {d0-d7}\n"
        "  vpush {d16-d31}\n"
        "  mov r0, lr\n"
        "  mrs r3, cpsr\n"
        "  cps #0x1f\n"                /* the program's lr and sp */
        "  mov r1, lr\n"
        "  mov r2, sp\n"
        "  msr cpsr_c, r3\n"
        "  bl samp_tick\n"
        "  vpop {d16-d31}\n"
        "  vpop {d0-d7}\n"
        "  pop {r2, r3}\n"
        "  vmsr fpscr, r2\n"
        "  pop {r0-r3, r12, lr}\n"
        "  movs pc, lr\n");

void samp_tick(uint32_t pc, uint32_t lr, uint32_t sp);
__attribute__((used)) void samp_tick(uint32_t pc, uint32_t lr, uint32_t sp)
{
    static unsigned n;
    const uint32_t id = ICCIAR;

    if ((id & 0x3ff) == PT_IRQ) {
        PT_ISR = 1;
        snd_pump();
        if (pg[C1Q3_W_PROF_ON]) {       /* the profile: every tick's pc */
            const uint32_t h = __builtin_bswap32(pg[C1Q3_W_PROF_HEAD]);

            ((volatile uint32_t *)(board + C1Q3_OFF + C1Q3_PROF_OFF))[h % C1Q3_PROF_N] = __builtin_bswap32(pc);
            pg[C1Q3_W_PROF_HEAD] = __builtin_bswap32(h + 1);
        }
        if (++n % PT_SAMPLE_EVERY == 0) {
            const uint32_t *w = (const uint32_t *)(sp & ~3u);
            int k = C1Q3_W_SAMPLE + 4, i;

            pg[C1Q3_W_SAMPLE + 1] = __builtin_bswap32(pc);
            pg[C1Q3_W_SAMPLE + 2] = __builtin_bswap32(lr);
            pg[C1Q3_W_SAMPLE + 3] = __builtin_bswap32(sp);
            for (i = 0; i < 4096 && k < C1Q3_W_SAMPLE_END && ((uint32_t)w & 0xFFF00000u) == (sp & 0xFFF00000u);
                 i++, w++)
                if (*w >= (uint32_t)_vector_table && *w < (uint32_t)__rodata_start)
                    pg[k++] = __builtin_bswap32(*w);
            while (k < C1Q3_W_SAMPLE_END)
                pg[k++] = 0;
            pg[C1Q3_W_SAMPLE] = __builtin_bswap32(__builtin_bswap32(pg[C1Q3_W_SAMPLE]) + 1);
        }
        /* -reload -force: the new image finds the interrupt off and answered */
        if (get(W_STATE) == HB_RELOAD) {
            PT_CTRL = 0;
            ICCEOIR = id;
            reload_posted();
        }
        ICCEOIR = id;
        return;
    }
    if ((id & 0x3ff) < 1020)
        ICCEOIR = id;
}

static void samp_start(void)
{
    __asm__ volatile("mcr p15, 0, %0, c12, c0, 0\n isb" : : "r"((uint32_t)samp_vectors));
    PT_CTRL = 0;
    PT_ISR = 1;
    PT_LOAD = (uint32_t)(tick_hz / (PT_PRESCALE + 1) / PT_HZ);  /* at any clock */
    snd.clock_t0 = ticks();
    if (!(ICDDCR & 1))                  /* only qemu: on the board core 0 has it on */
        ICDDCR = 1;
    ICDIPR_29 = 0xA0;
    ICDISER0 = 1u << PT_IRQ;
    ICCPMR = 0xF0;
    ICCICR = 1;
    PT_CTRL = PT_ON;
    __asm__ volatile("cpsie i");
}

static void samp_stop(void)
{
    __asm__ volatile("cpsid i");
    PT_CTRL = 0;
    PT_ISR = 1;
    ICDICER0 = 1u << PT_IRQ;
    __asm__ volatile("mcr p15, 0, %0, c12, c0, 0\n isb" : : "r"((uint32_t)_vector_table));
}

/* --- load per second, as ztop reads it --- */
static uint64_t win_start, win_idle;
static uint32_t win_max_us;

static void load_window(uint64_t now)
{
    const uint64_t span = now - win_start;

    if (span < tick_hz)
        return;
    put(W_LOAD, (uint32_t)(1000 - (win_idle > span ? span : win_idle) * 1000 / span));
    put(W_HEARTBEAT, get(W_HEARTBEAT) + 1);
    put(W_NOW, (uint32_t)now);
    qput(C1Q3_W_HEAP_KB, heap_left_kb());
    qput(C1Q3_W_FRAME_MS_MAX, win_max_us / 1000);
    win_max_us = 0;
    win_start = now;
    win_idle = 0;
}

/* --- the picture --- */
void c1p_present(const uint8_t *rgba, int w, int h)
{
    volatile uint32_t *c1ld = (volatile uint32_t *)(board + C1LD_OFF);
    const uint32_t opitch = qget(C1Q3_W_OVL_PITCH), row0 = qget(C1Q3_W_OVL_ROW0);
    const uint32_t maxw = qget(C1Q3_W_OVL_MAXW), maxh = qget(C1Q3_W_OVL_MAXH);
    static uint32_t line[2048];
    uint32_t back;
    volatile uint8_t *dst;
    int x, y, ow = w, oh = h, dbuf;

    qput(C1Q3_W_WIDTH, (uint32_t)w);
    qput(C1Q3_W_HEIGHT, (uint32_t)h);
    if (!opitch)
        return;                         /* no window yet */
    dbuf = (be(c1ld[C1LD_W_FEAT]) & C1LD_FEAT_OVL_FLIP) != 0;
    if (dbuf) {                         /* the last flip not taken yet: wait (at most 40 ms) */
        const uint64_t t0 = ticks();

        while (c1ld[C1LD_W_FLIP] != 0 && ticks() - t0 < tick_hz / 25)
            ;
        win_idle += ticks() - t0;
    }
    if ((uint32_t)ow > maxw)
        ow = (int)maxw;
    if ((uint32_t)oh > maxh)
        oh = (int)maxh;
    if (ow <= 0 || oh <= 0 || ow > 2048 || (uint32_t)ow * 4 > opitch)
        return;
    back = !dbuf || be(c1ld[C1LD_W_FRONT]) == 2 ? C1Q3_OVL0_OFF : C1Q3_OVL1_OFF;
    dst = board + back + row0 * opitch;
    for (y = 0; y < oh; y++) {
        const uint32_t *src = (const uint32_t *)rgba + (size_t)(h - 1 - y) * (size_t)w;

        for (x = 0; x < ow; x++) {
            const uint32_t p = src[x];  /* r, g, b, a bytes */
            const uint32_t q = (p & 0xff) << 16 | (p & 0xff00) | (p >> 16 & 0xff);

            line[x] = q ? q : 0x010101; /* 0 is transparent in the overlay */
        }
        memcpy((void *)(dst + (uint32_t)y * opitch), line, (size_t)ow * 4);
    }
    if (dbuf)
        c1ld[C1LD_W_FLIP] = be(back == C1Q3_OVL0_OFF ? 1 : 2);
}

void c1p_set_mouse(int grab)
{
    qput(C1Q3_W_MOUSE, grab ? 1 : 0);
}

/* --- input from the ring --- */
int c1p_next_event(uint32_t *ev)
{
    const uint32_t tail = qget(C1Q3_W_IN_TAIL);

    if (tail == qget(C1Q3_W_IN_HEAD))
        return 0;
    *ev = be(((const volatile uint32_t *)(board + C1Q3_OFF + C1Q3_IN_OFF))[tail % C1Q3_IN_N]);
    qput(C1Q3_W_IN_TAIL, tail + 1);
    return 1;
}

/* --- files: in at start, written ones back --- */
static void text(char *buf, size_t n)
{
    size_t i;

    for (i = 0; i + 1 < n && i < C1Q3_TEXT_LEN; i++)
        if (!(buf[i] = (char)board[C1Q3_OFF + C1Q3_TEXT_OFF + i]))
            return;
    buf[i] = 0;
}

static void open_file(void)
{
    char name[256];
    int f;

    text(name, sizeof(name));
    if (name[0] != '/')
        f = -1;
    else if (qget(C1Q3_W_FLAGS) & C1Q3_FILE_REMOTE)
        f = c1fs_add_remote(name, qget(C1Q3_W_SIZE));
    else
        f = c1fs_add(name, qget(C1Q3_W_SIZE), (int)qget(C1Q3_W_FLAGS));
    if (f < 0)
        qput(C1Q3_W_ERR, C1Q3_E_NOMEM);
    qput(C1Q3_W_RESULT, (uint32_t)f);
}

static void chunk(void)
{
    const uint32_t f = qget(C1Q3_W_FILE), off = qget(C1Q3_W_OFF), len = qget(C1Q3_W_LEN);

    if (f >= C1FS_MAX || !c1fs[f].name || c1fs[f].remote || len > C1Q3_CHUNK_MAX || (size_t)off + len > c1fs[f].size) {
        qput(C1Q3_W_RESULT, (uint32_t)-1);
        return;
    }
    memcpy(c1fs[f].data + off, (const void *)(board + C1Q3_WIN_OFF), len);
    qput(C1Q3_W_RESULT, 0);
}

/* a block of a REMOTE file from the 68k (c1q3 reads it into C1Q3_REQ_OFF);
   waiting counts as idle */
int c1p_fetch(int file, uint32_t off, uint32_t len, void *dst)
{
    const uint32_t seq = qget(C1Q3_W_REQ_SEQ) + 1;
    const uint64_t t0 = ticks();
    uint32_t r;

    if (len > C1Q3_REQ_MAX)
        len = C1Q3_REQ_MAX;
    qput(C1Q3_W_REQ_FILE, (uint32_t)file);
    qput(C1Q3_W_REQ_OFF, off);
    qput(C1Q3_W_REQ_LEN, len);
    qput(C1Q3_W_REQ_SEQ, seq);
    while (qget(C1Q3_W_REQ_DONE) != seq) {
        if (ticks() - t0 > 60 * tick_hz) {
            c1p_log("c1q3: no answer to a file request for 60 s\n");
            return -1;
        }
        reload_posted();
    }
    win_idle += ticks() - t0;
    load_window(ticks());
    r = qget(C1Q3_W_REQ_RESULT);
    if (r > len)
        return -1;
    memcpy(dst, (const void *)(board + C1Q3_REQ_OFF), r);
    return (int)r;
}

/* the next piece of a written file into the window */
static struct { int file; size_t off; } fl = { -1, 0 };

static void flush_one(void)
{
    struct c1file *f;
    size_t len, i;

    if (fl.file < 0 || !c1fs[fl.file].name) {
        fl.file = c1fs_next_dirty();
        fl.off = 0;
    }
    if (fl.file < 0) {
        qput(C1Q3_W_RESULT, (uint32_t)-1);
        qput(C1Q3_W_DIRTY, 0);
        return;
    }
    f = &c1fs[fl.file];
    for (i = 0; f->name[i] && i + 1 < C1Q3_TEXT_LEN; i++)
        board[C1Q3_OFF + C1Q3_TEXT_OFF + i] = (uint8_t)f->name[i];
    board[C1Q3_OFF + C1Q3_TEXT_OFF + i] = 0;
    qput(C1Q3_W_FILE, (uint32_t)fl.file);
    if (f->removed) {
        qput(C1Q3_W_SIZE, C1Q3_REMOVED);
        qput(C1Q3_W_OFF, 0);
        qput(C1Q3_W_LEN, 0);
        qput(C1Q3_W_RESULT, 0);
        free(f->name);
        f->name = NULL;                 /* the slot is free now */
        fl.file = -1;
        qput(C1Q3_W_DIRTY, c1fs_dirty_count());
        return;
    }
    if (fl.off == 0)
        f->dirty = 0;                   /* a write from now on sends it again */
    if (fl.off > f->size)
        fl.off = f->size;
    len = f->size - fl.off < C1Q3_CHUNK_MAX ? f->size - fl.off : C1Q3_CHUNK_MAX;
    memcpy((void *)(board + C1Q3_WIN_OFF), f->data + fl.off, len);
    qput(C1Q3_W_SIZE, (uint32_t)f->size);
    qput(C1Q3_W_OFF, (uint32_t)fl.off);
    qput(C1Q3_W_LEN, (uint32_t)len);
    fl.off += len;
    if (fl.off >= f->size) {
        qput(C1Q3_W_RESULT, 0);
        fl.file = -1;
    } else
        qput(C1Q3_W_RESULT, 1);
    qput(C1Q3_W_DIRTY, c1fs_dirty_count() + (fl.file >= 0));
}

/* --- while the engine runs: the 68k's commands --- */
static char exec_text[C1Q3_TEXT_LEN + 1];

const char *c1p_poll(void)
{
    const uint32_t cmd = qget(C1Q3_W_CMD);
    const char *r = NULL;

    if (reload_posted())
        return NULL;
    switch (cmd) {
    case C1Q3_CMD_NONE:
        return NULL;
    case C1Q3_CMD_EXEC:
        text(exec_text, sizeof(exec_text));
        qput(C1Q3_W_RESULT, 0);
        r = exec_text;
        break;
    case C1Q3_CMD_STOP:
        qput(C1Q3_W_RESULT, 0);
        r = "quit";
        break;
    case C1Q3_CMD_FLUSH:
        flush_one();
        break;
    default:
        qput(C1Q3_W_RESULT, (uint32_t)-9);  /* not now */
    }
    qput(C1Q3_W_CMD, 0);
    return r;
}

void c1p_sleep_us(unsigned us)
{
    const uint64_t t0 = ticks(), end = t0 + (uint64_t)us * tick_hz / 1000000ULL;

    while (ticks() < end)
        __asm__ volatile("nop");
    win_idle += ticks() - t0;
    load_window(ticks());
}

void c1p_frame_done(void)
{
    static uint64_t last, avg;
    const uint64_t now = ticks();

    if (last) {
        const uint32_t us = (uint32_t)((now - last) * 1000000ULL / tick_hz);

        avg = avg ? (avg * 31 + us) / 32 : us;
        qput(C1Q3_W_FRAME_US, us);
        qput(C1Q3_W_FRAME_US_AVG, (uint32_t)avg);
        if (us > win_max_us)
            win_max_us = us;
    }
    last = now;
    qput(C1Q3_W_FRAMES, qget(C1Q3_W_FRAMES) + 1);
    put(W_PROGRESS, qget(C1Q3_W_FRAMES));
    qput(C1Q3_W_DIRTY, c1fs_dirty_count() + (fl.file >= 0));
    load_window(now);
}

/* after the game: written files go out, then a reload */
static void wait_reload(void) __attribute__((noreturn));
static void wait_reload(void)
{
    c1p_snd_stop();
    qput(C1Q3_W_DIRTY, c1fs_dirty_count() + (fl.file >= 0));
    put(W_STATE, HB_DONE);
    for (;;) {
        const uint32_t cmd = qget(C1Q3_W_CMD);

        reload_posted();
        if (cmd == C1Q3_CMD_FLUSH)
            flush_one();
        else if (cmd != C1Q3_CMD_NONE)
            qput(C1Q3_W_RESULT, (uint32_t)-9);
        if (cmd != C1Q3_CMD_NONE)
            qput(C1Q3_W_CMD, 0);
        c1p_sleep_us(1000);
    }
}

void c1p_quit(void)
{
    qput(C1Q3_W_STATE, C1Q3_QUIT);
    wait_reload();
}

void c1p_fail(const char *why)
{
    c1p_log("c1q3: ");
    c1p_log(why);
    c1p_log("\n");
    qput(C1Q3_W_ERR, C1Q3_E_ENGINE);
    qput(C1Q3_W_STATE, C1Q3_FAILED);
    wait_reload();
}

static void run(void)
{
    static char cmdline[C1Q3_TEXT_LEN + 1];

    text(cmdline, sizeof(cmdline));
    qput(C1Q3_W_FRAMES, 0);
    qput(C1Q3_W_UNDERRUNS, 0);
    snd.volume = qget(C1Q3_W_VOLUME) && qget(C1Q3_W_VOLUME) <= 256 ? qget(C1Q3_W_VOLUME) : 256;
    memset((void *)(board + C1Q3_HDMI_OFF), 0, C1Q3_HDMI_SIZE);
    qput(C1Q3_W_STATE, C1Q3_RUNNING);
    qput(C1Q3_W_RESULT, 0);
    qput(C1Q3_W_CMD, 0);
    put(W_STATE, HB_RUNNING);
    c1q3_main(cmdline);
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
    for (i = 0x182; i < 0x1FE; i++)             /* board memory */
        set_section(i * 0x100000UL, RTG_POLICY);
    set_section(RTG_BASE + HB_CTL_OFF, NORM_NONCACHE);     /* also C1Q3_OFF and C1LD_OFF */
    for (i = 0; i < (int)(HB_IMAGE_MAX >> 20); i++)
        set_section(RTG_BASE + HB_DATA_OFF + i * 0x100000UL, NORM_NONCACHE);
    for (i = 0; i < (int)(C1Q3_WIN_SIZE >> 20); i++)
        set_section(RTG_BASE + C1Q3_WIN_OFF + i * 0x100000UL, NORM_NONCACHE);
    set_section(RTG_BASE + C1Q3_HDMI_OFF, NORM_NONCACHE);  /* the DMA reads DDR */
    set_section(RELOAD_TRAMP, NORM_NONCACHE);
    set_section(0xFFF00000UL, NORM_NONCACHE);
    mmu_sync();
    put(W_STAGE, 4);

    for (i = 0; i < 4; i++)
        put(W_NAME + i, (uint32_t)core_name[i * 4] << 24 | (uint32_t)core_name[i * 4 + 1] << 16 |
                        (uint32_t)core_name[i * 4 + 2] << 8 | (uint32_t)core_name[i * 4 + 3]);
    put(W_VERSION, Q3_VERSION);
    put(W_PARTS, HB_PART_GAME);
    if (cpu_hz())
        tick_hz = cpu_hz() / 2;
    put(W_CPU_HZ, cpu_hz());
    put(W_TICK_HZ, (uint32_t)tick_hz);
    for (i = 0; i < C1Q3_W_NWORDS; i++)
        pg[i] = 0;
    qput(C1Q3_W_VERSION, C1Q3_VERSION);
    qput(C1Q3_W_CPU_HZ, cpu_hz());
    qput(C1Q3_W_TICK_HZ, (uint32_t)tick_hz);
    qput(C1Q3_W_STATE, C1Q3_IDLE);
    qput(C1Q3_W_HEAP_KB, heap_left_kb());
    snd.last_pos = Xil_In32(C1Q3_HDMI_POS_REG) & ~3u;
    if (snd.last_pos >= C1Q3_HDMI_SIZE)
        snd.last_pos = 0;
    qput(C1Q3_W_MAGIC, C1Q3_MAGIC);
    put(W_STATE, HB_IDLE);
    put(W_LOAD, 0);
    win_start = ticks();
    put(W_MAGIC, HB_MAGIC_READY);
    put(W_STAGE, 5);
    CORE0_ACK = 0;
    samp_start();

    for (;;) {
        const uint32_t cmd = qget(C1Q3_W_CMD);

        if (reload_posted())
            continue;
        if (cmd == C1Q3_CMD_OPEN) {
            qput(C1Q3_W_STATE, C1Q3_LOADING);
            open_file();
            qput(C1Q3_W_HEAP_KB, heap_left_kb());
            qput(C1Q3_W_CMD, 0);
        } else if (cmd == C1Q3_CMD_CHUNK) {
            chunk();
            qput(C1Q3_W_CMD, 0);
        } else if (cmd == C1Q3_CMD_START) {
            run();                      /* not back */
        } else if (cmd != C1Q3_CMD_NONE) {
            qput(C1Q3_W_RESULT, (uint32_t)-9);
            qput(C1Q3_W_CMD, 0);
        } else
            c1p_sleep_us(1000);
    }
}
