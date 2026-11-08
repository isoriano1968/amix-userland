/*
 * host_main.c -- uae4arm's core 1 build, tested on the PC: the same ARM
 * code under qemu-arm -cpu cortex-a9, files through semihosting.
 *
 *   qemu-arm -cpu cortex-a9 uaehost.elf [options] SYSDIR CONTENT
 *     -var KEY=VALUE   a core option (uae4arm_model=A1200 ...)
 *     -cfg LINE        a uae config line (kickstart_rom_file=..., floppy1=...)
 *     -frames N        run N frames (default 500)
 *     -ppm K DIR       every K-th frame as DIR/fNNNNN.ppm
 *     -wav FILE        the sound, 48 kHz stereo
 *     -joy F N BITS    port 1 joypad BITS (RETRO_DEVICE_ID_JOYPAD_* bits) from frame F, N frames
 *     -key F N CODE    RETROK_ CODE held from frame F, N frames
 *   qemu-arm -cpu cortex-a9 uaehost.elf @ARGFILE
 *     the same, one argument per line (semihosting's command line is short)
 *
 * The clock is virtual: 20 ms a frame (PAL), 1 ms more whenever the
 * emulator waits and 5 us per look, so a run gives the same frames every time.  Every frame
 * prints a checksum: the board's run of the same image must match it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#include "c1uae.h"

static FILE *wav;
static uint32_t wav_bytes;
static unsigned ppm_every;
static const char *ppm_dir;
static uint64_t vclock;
uint32_t c1_dbg_pc(void);
uint32_t c1_dbg_chip_long(uint32_t addr);
void c1_dbg_chip_str(uint32_t addr, char *buf, unsigned n);

/* c1bench.adf's marks (files/bench/amiga/core_portme.h) */
#define C1BENCH_MARK 0x7FF00
#define C1BENCH_TEXT 0x7E000
static uint32_t sum;

#define MAX_EV 64
static struct { unsigned f, n, port, kind; unsigned v; } ev[MAX_EV];
static int nev;

/*
 * The heap.  newlib's semihosting _sbrk refuses to grow past the stack
 * pointer, and the emulator runs on a libco stack that lies in the heap:
 * a fixed arena instead, as the board has (arm_main.c).
 */
#define HOST_HEAP   (192u << 20)
static char heap[HOST_HEAP] __attribute__((aligned(4096)));
static size_t brk_used;

void *_sbrk(ptrdiff_t incr);
void *_sbrk(ptrdiff_t incr)
{
    char *p = heap + brk_used;

    if (incr < 0 || (size_t)incr > HOST_HEAP - brk_used)
        return (void *)-1;
    brk_used += (size_t)incr;
    return p;
}

void c1p_log(const char *s)
{
    fputs(s, stdout);
}

/* each look at the clock costs 5 us of it: uae's "fastest CPU" runs the
   68k until the frame's time is used up, which a clock that only moves
   between frames would never say */
uint64_t c1p_now_us(void)
{
    vclock += 5;
    return vclock;
}

void c1p_idle(void)
{
    vclock += 1000;
}

static void wav_header(uint32_t data)
{
    uint8_t h[44];
    uint32_t rate = C1UAE_RATE, br = C1UAE_RATE * 4;

    memcpy(h, "RIFF", 4);
    h[4] = (uint8_t)(data + 36); h[5] = (uint8_t)((data + 36) >> 8);
    h[6] = (uint8_t)((data + 36) >> 16); h[7] = (uint8_t)((data + 36) >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = h[18] = h[19] = 0;
    h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
    h[24] = (uint8_t)rate; h[25] = (uint8_t)(rate >> 8); h[26] = (uint8_t)(rate >> 16); h[27] = 0;
    h[28] = (uint8_t)br; h[29] = (uint8_t)(br >> 8); h[30] = (uint8_t)(br >> 16); h[31] = 0;
    h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)data; h[41] = (uint8_t)(data >> 8); h[42] = (uint8_t)(data >> 16); h[43] = (uint8_t)(data >> 24);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
    fseek(wav, 0, SEEK_END);
}

void c1p_audio(const int16_t *lr, size_t frames)
{
    if (wav) {
        fwrite(lr, 4, frames, wav);         /* little-endian, as WAV wants */
        wav_bytes += (uint32_t)frames * 4;
    }
}

void c1p_video(const uint16_t *pix, unsigned w, unsigned h, size_t pitch)
{
    unsigned x, y;

    sum = 0;
    for (y = 0; y < h; y++) {
        const uint16_t *row = (const uint16_t *)((const uint8_t *)pix + y * pitch);

        for (x = 0; x < w; x++)
            sum = sum * 31 + row[x];
    }
    if (ppm_every && c1_frames % ppm_every == 0) {
        char name[256];
        FILE *f;

        snprintf(name, sizeof(name), "%s/f%05u.ppm", ppm_dir, (unsigned)c1_frames);
        f = fopen(name, "wb");
        if (f) {
            static uint8_t line[3 * 2048];

            fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (y = 0; y < h; y++) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)pix + y * pitch);

                for (x = 0; x < w && x < 2048; x++) {
                    const uint16_t p = row[x];

                    line[x * 3] = (uint8_t)((p >> 11) << 3 | (p >> 13));
                    line[x * 3 + 1] = (uint8_t)(((p >> 5) & 63) << 2 | ((p >> 9) & 3));
                    line[x * 3 + 2] = (uint8_t)((p & 31) << 3 | ((p >> 2) & 7));
                }
                fwrite(line, 3, x, f);
            }
            fclose(f);
        }
    }
}

void c1p_input(void)
{
    int i;

    c1_joy[0] = c1_joy[1] = 0;
    memset(c1_key, 0, sizeof(c1_key));
    for (i = 0; i < nev; i++)
        if (c1_frames >= ev[i].f && c1_frames < ev[i].f + ev[i].n) {
            if (ev[i].kind == 0)
                c1_joy[ev[i].port] |= (uint16_t)ev[i].v;
            else if (ev[i].v < 512)
                c1_key[ev[i].v] = 1;
        }
}

/* @FILE: the arguments, one per line */
static int argfile(const char *name, char ***argvp)
{
    static char *av[256];
    static char buf[8192];
    FILE *f = fopen(name, "r");
    int n = 1;
    size_t len;
    char *p;

    if (!f)
        return 0;
    len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = 0;
    av[0] = "uaehost";
    for (p = strtok(buf, "\n"); p && n < 255; p = strtok(NULL, "\n"))
        if (*p)
            av[n++] = p;
    av[n] = NULL;
    *argvp = av;
    return n;
}

int main(int argc, char **argv)
{
    unsigned frames = 500, f;
    clock_t t0;
    int i;

    if (argc == 2 && argv[1][0] == '@')
        argc = argfile(argv[1] + 1, &argv);

    for (i = 1; i < argc - 2; i++) {
        if (!strcmp(argv[i], "-var") && i + 1 < argc - 2) {
            char k[64], *eq;

            snprintf(k, sizeof(k), "%s", argv[++i]);
            eq = strchr(k, '=');
            if (eq) {
                *eq = 0;
                c1uae_set_var(k, eq + 1);
            }
        } else if (!strcmp(argv[i], "-cfg") && i + 1 < argc - 2)
            c1uae_add_line(argv[++i]);
        else if (!strcmp(argv[i], "-frames") && i + 1 < argc - 2)
            frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-ppm") && i + 2 < argc - 2) {
            ppm_every = (unsigned)atoi(argv[++i]);
            ppm_dir = argv[++i];
        } else if (!strcmp(argv[i], "-wav") && i + 1 < argc - 2) {
            wav = fopen(argv[++i], "wb");
            if (wav)
                wav_header(0);
        } else if ((!strcmp(argv[i], "-joy") || !strcmp(argv[i], "-key")) && i + 3 < argc - 2 && nev < MAX_EV) {
            ev[nev].kind = argv[i][1] == 'k';
            ev[nev].port = 0;      /* libretro port 0 = the Amiga's joystick port 2 */
            ev[nev].f = (unsigned)atoi(argv[i + 1]);
            ev[nev].n = (unsigned)atoi(argv[i + 2]);
            ev[nev].v = (unsigned)strtoul(argv[i + 3], NULL, 0);
            nev++;
            i += 3;
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (argc < 3) {
        fprintf(stderr, "usage: uaehost.elf [options] SYSDIR CONTENT\n");
        return 2;
    }
    t0 = clock();
    if (c1uae_start(argv[argc - 2], argv[argc - 1]) < 0) {
        fprintf(stderr, "the core refused %s\n", argv[argc - 1]);
        return 1;
    }
    for (f = 0; f < frames; f++) {
        vclock += 20000;
        c1uae_frame();
        {
            static uint32_t mark;
            const uint32_t m = c1_dbg_chip_long(C1BENCH_MARK);

            if (m != mark && (m >> 8) == 0x433142) {     /* "C1B?" */
                printf("frame %u: bench mark %c\n", (unsigned)c1_frames, (char)m);
                if (m == 0x43314244) {
                    static char text[8192];

                    c1_dbg_chip_str(C1BENCH_TEXT, text, sizeof(text));
                    printf("%s\n", text);
                    break;
                }
            }
            mark = m;
        }
        if (f % 100 == 0 || f + 1 == frames)
            printf("frame %u sum %08x pc %08x\n", (unsigned)c1_frames, (unsigned)sum, (unsigned)c1_dbg_pc());
    }
    if (wav) {
        wav_header(wav_bytes);
        fclose(wav);
    }
    printf("done: %u frames, %.1f s host (qemu, not the A9's speed)\n", frames,
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    return 0;
}
