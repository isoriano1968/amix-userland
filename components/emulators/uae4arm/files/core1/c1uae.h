/*
 * c1uae.h -- uae4arm on the Z3660's core 1: what the frontend (c1uae.c)
 * and the two platforms share.
 *
 *   host_main.c  -- the PC test: qemu-arm, files through semihosting,
 *                   pictures to PPM, sound to WAV, a virtual clock
 *   core1_main.c -- the board: control block, rings to AMIX (files, input),
 *                   picture into the RTG overlay, sound into the HDMI ring
 *
 * The emulator itself is upstream's libretro core (libretro-core.cpp); the
 * frontend is a minimal libretro frontend.
 */
#ifndef C1UAE_H
#define C1UAE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- the frontend (c1uae.c) --- */

/* before c1uae_start: a libretro core option (uae4arm_model ...) */
void c1uae_set_var(const char *key, const char *value);
/* before c1uae_start: one uae config line (kickstart_rom_file=..., floppy1=...) */
void c1uae_add_line(const char *line);
/* the libretro "system" directory (the core's own files) and the content
   (DF0 or a hard file); returns 0 when the core took it */
int c1uae_start(const char *sysdir, const char *content);
/* one emulated frame: input in, picture and sound out */
void c1uae_frame(void);

/* the frontend's state the platforms fill */
extern uint8_t c1_key[512];             /* RETROK_* pressed */
extern uint16_t c1_joy[2];              /* RETRO_DEVICE_ID_JOYPAD_* bits per port */
extern int16_t c1_mouse_dx, c1_mouse_dy;/* since the last frame */
extern uint8_t c1_mouse_btn;            /* bit 0 left, 1 right, 2 middle */
extern uint32_t c1_frames;

/* --- the platform --- */

/* the frame, RGB565, as the core renders it */
void c1p_video(const uint16_t *pix, unsigned w, unsigned h, size_t pitch);
/* interleaved stereo 16-bit samples at C1UAE_RATE */
void c1p_audio(const int16_t *lr, size_t frames);
/* input for the next frame into c1_key/c1_joy/c1_mouse_* */
void c1p_input(void);
/* nothing runnable: the emulator waits on time or the 68k */
void c1p_idle(void);
/* microseconds of the platform's clock */
uint64_t c1p_now_us(void);
/* say something (the host's stdout, core 1's log ring) */
void c1p_log(const char *s);

/* c1_thread.cpp: run uae's helper threads once */
void c1_run_workers(void);

#define C1UAE_RATE  48000               /* the HDMI ring's rate: no resampling */

#ifdef __cplusplus
}
#endif
#endif
