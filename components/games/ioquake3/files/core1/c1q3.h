/*
 * c1q3.h -- ioquake3 on the Z3660's core 1: what the platform (core1_main.c,
 * the harness) gives the engine's glue (sys_c1.c, glimp_c1.c, snd_c1.c,
 * in_c1.c).  The harness knows nothing of the engine; the glue nothing of
 * the board.
 */
#ifndef C1Q3_H
#define C1Q3_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- the engine side (sys_c1.c): the command line from AMIX; never returns --- */
void c1q3_main(const char *cmdline) __attribute__((noreturn));

/* --- the platform (core1_main.c) --- */

/* memory for a big file that stays for the whole run (a pak), from the
   top of the high heap region; NULL: use malloc */
void *c1p_file_alloc(size_t size);
/* say something: AMIX's log ring */
void c1p_log(const char *s);
/* microseconds and milliseconds of core 1's clock (from the PLL) */
uint64_t c1p_now_us(void);
/* between frames and while waiting: the 68k's commands; a console command
   from it (CMD_EXEC), or NULL */
const char *c1p_poll(void);
/* the next input event from the ring (c1q3_board.h C1Q3_EV_*): 1, or 0 */
int c1p_next_event(uint32_t *ev);
/* wait so long, counted as idle for the load figure; polls meanwhile */
void c1p_sleep_us(unsigned us);
/* a frame is done: its time, the load per second */
void c1p_frame_done(void);
/* the frame (OSMesa RGBA, bottom row first) into the overlay */
void c1p_present(const uint8_t *rgba, int w, int h);
/* the game takes the mouse (1) or lets it go (0): the 68k grabs the pointer */
void c1p_set_mouse(int grab);
/* the game has quit / failed: tell AMIX, then wait for a reload */
void c1p_quit(void) __attribute__((noreturn));
void c1p_fail(const char *why) __attribute__((noreturn));

/* sound: the engine mixes 16-bit stereo at C1P_RATE into RING (FRAMES, a
   power of 2) up to *PAINTED (its frame counter); the platform's timer
   interrupt carries it on into the HDMI ring.  c1p_snd_now() is the frame
   being played, in the engine's count. */
#define C1P_RATE 48000
void c1p_snd_start(int16_t *ring, uint32_t frames, volatile int *painted);
void c1p_snd_stop(void);
uint32_t c1p_snd_now(void);

#ifdef __cplusplus
}
#endif
#endif
