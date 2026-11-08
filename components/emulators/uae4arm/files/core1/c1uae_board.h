/*
 * c1uae_board.h -- z3660ARM_Core_uae: what core 1 and AMIX (c1uae, the
 * front end's helper) share in the Z3660's board memory.  Offsets are from
 * the start of board memory: the ARM sees them at 0x18000000 + offset, the
 * 68k through /dev/zz9000 (mmap offset = board offset - 2 MB).  Words are
 * big-endian (the 68k's order); the ARM swaps.
 *
 * The image runs alone on core 1 (not in the combined amix core), so it
 * takes the player's areas: its control page, the clip window, the overlay
 * buffers and the HDMI ring.  The rest of the harness is bench.h's
 * (control block HB_CTL_OFF, loader C1LD_OFF, -load/-reload).
 *
 * Start-up (c1uae -run):
 *   1. armbench -load/-reload z3660ARM_Core_uae.bin, wait for HB_MAGIC_READY
 *   2. the 68k fills C1UA_W_* (config text, files), state C1UA_LOADING,
 *      then per file: up to 6 MB at a time into the window (C1UA_WIN_OFF),
 *      C1UA_W_CHUNK_* and CMD_CHUNK; core 1 copies it into its own memory
 *      and clears the command
 *   3. CMD_START: core 1 boots the Amiga, state C1UA_RUNNING
 *   4. while it runs: input events into the ring (68k writes, core 1 reads),
 *      at most a few polls a second of C1UA_W_DIRTY by the 68k (every 68k
 *      read of board memory stalls core 0), CMD_FLUSH to get a changed
 *      block of a writable file back through the window
 *   5. CMD_STOP: core 1 leaves the Amiga, state C1UA_IDLE; -reload works
 *
 * Polling: the 68k reads this page at <= 10 Hz (z3660-firmware-video note:
 * its reads stall core 0's main loop); writes are cheap.
 */
#ifndef C1UAE_BOARD_H
#define C1UAE_BOARD_H

#define C1UA_OFF            0x07AFD000UL    /* the control page (ZZAV_OFF: no player in this image) */
#define C1UA_MAGIC          0x43315541UL    /* "C1UA": the core runs and listens */
#define C1UA_VERSION        0x00000100UL    /* 0.1.0 of this protocol */

/* the data window for files: the player's packet ring (ZZAV_RING_OFF,
   6 MB, uncached on both cores, the way 68k data reaches core 1 in vplay) */
#define C1UA_WIN_OFF        0x07100000UL
#define C1UA_WIN_SIZE       0x00600000UL
/* the overlay's two buffers (as the player's ZZAV_OVL_OFF, OFF1) */
#define C1UA_OVL0_OFF       0x06000000UL
#define C1UA_OVL1_OFF       0x06800000UL
/* the board's HDMI sound ring, 48 kHz 16-bit stereo, little-endian, played
   by the audio formatter's DMA (started by the 68k: REG_ZZ_AUDIO_PARAM 0,
   VAL 0x07CE0000, as vplay) */
#define C1UA_HDMI_OFF       0x07CE0000UL
#define C1UA_HDMI_SIZE      30720UL
#define C1UA_HDMI_POS_REG   0x7FC00128UL    /* the DMA's byte position in it (ARM address) */

/* C1UA_W_STATE */
enum { C1UA_IDLE, C1UA_LOADING, C1UA_RUNNING, C1UA_STOPPED, C1UA_FAILED };

/* C1UA_W_CMD, from the 68k; core 1 sets it back to 0 when done (result in C1UA_W_RESULT) */
enum {
    C1UA_CMD_NONE,
    C1UA_CMD_CHUNK,         /* C1UA_W_CHUNK_FILE/_OFF/_LEN bytes from the window */
    C1UA_CMD_START,         /* boot: config text at C1UA_TEXT_OFF, content = file 0 */
    C1UA_CMD_STOP,
    C1UA_CMD_FLUSH,         /* next changed block of a writable file into the window */
    C1UA_CMD_RESET,         /* the Amiga's reset (keyboard reset) */
    C1UA_CMD_PAUSE,         /* C1UA_W_ARG 1 pause, 0 go on */
    C1UA_CMD_BENCH          /* as START, but no pacing: frames as fast as they come,
                               C1UA_W_ARG frames (or until c1bench.adf says done),
                               then STOPPED with the times */
};

/* files: up to C1UA_MAX_FILES, by name (what the config text says); the
   names are paths as the front end gives them, the core needs no file system */
#define C1UA_MAX_FILES      8
#define C1UA_NAME_LEN       120
#define C1UA_FILE_RW        1               /* flag: changes come back (CMD_FLUSH) */
#define C1UA_FILE_SWAB      2               /* flag: 16-bit swapped ROM image (.bin for EPROMs) */

/* the 32-bit words of the page */
enum {
    C1UA_W_MAGIC, C1UA_W_VERSION, C1UA_W_STATE, C1UA_W_CMD, C1UA_W_ARG, C1UA_W_RESULT,
    C1UA_W_FRAMES,              /* core 1: frames since START */
    C1UA_W_FRAME_US,            /* core 1: emulation time of the last frame, us */
    C1UA_W_FRAME_US_AVG,        /* core 1: average over the last 50 */
    C1UA_W_SKIPPED,             /* core 1: frames not shown (too slow) */
    C1UA_W_UNDERRUNS,           /* core 1: sound ran dry */
    C1UA_W_CPU_HZ,              /* core 1: the ARM clock it measured with */
    C1UA_W_BENCH_TICKS_LO, C1UA_W_BENCH_TICKS_HI,   /* CMD_BENCH: timer ticks for the frames */
    C1UA_W_TICK_HZ,
    C1UA_W_PC,                  /* the 68k's PC (debugging) */
    C1UA_W_CHUNK_FILE, C1UA_W_CHUNK_OFF, C1UA_W_CHUNK_LEN,
    C1UA_W_DIRTY,               /* core 1: changed blocks waiting (any file) */
    C1UA_W_FLUSH_FILE, C1UA_W_FLUSH_OFF, C1UA_W_FLUSH_LEN,  /* core 1: what CMD_FLUSH put in the window */
    C1UA_W_OVL_PITCH, C1UA_W_OVL_ROW0, C1UA_W_OVL_MAXW, C1UA_W_OVL_MAXH,  /* 68k: where the picture goes */
    C1UA_W_OVL_SCALE,           /* 68k: 1 or 2 (pixel doubling, the overlay has no scaler) */
    C1UA_W_VOLUME,              /* 68k: 0-256 */
    C1UA_W_IN_HEAD,             /* 68k: next input event it writes */
    C1UA_W_IN_TAIL,             /* core 1: next input event it reads */
    C1UA_W_LOG_HEAD,            /* core 1: bytes written into the log ring */
    C1UA_W_ERR,                 /* core 1: why C1UA_FAILED (C1UA_E_*) */
    C1UA_W_BM_STATE,            /* CMD_BENCH with c1bench.adf: the last mark seen ('S', 'E', 'D') */
    C1UA_W_BM_FRAMES,           /* emulated frames from 'S' to 'D' */
    C1UA_W_BM_TICKS_LO, C1UA_W_BM_TICKS_HI,  /* timer ticks from 'S' to 'D' */
    C1UA_W_FILES = 64,          /* C1UA_MAX_FILES x { size, flags, crc32 }, names at C1UA_NAMES_OFF */
    C1UA_W_NWORDS = C1UA_W_FILES + C1UA_MAX_FILES * 3
};

enum { C1UA_E_NONE, C1UA_E_NOMEM, C1UA_E_FILE, C1UA_E_CONFIG, C1UA_E_CORE };

/* byte offsets inside the page */
#define C1UA_NAMES_OFF      0x200           /* C1UA_MAX_FILES x C1UA_NAME_LEN */
#define C1UA_IN_OFF         0x600           /* input ring: C1UA_IN_N events of 4 bytes */
#define C1UA_IN_N           256
#define C1UA_TEXT_OFF       0xA00           /* config text: "key=value\n" lines, NUL-ended */
#define C1UA_TEXT_LEN       0x400
#define C1UA_LOG_OFF        0xE00           /* core 1's messages, a ring of C1UA_LOG_LEN bytes */
#define C1UA_LOG_LEN        0x200

/*
 * Input events: one 32-bit word, type << 24 | data
 *   KEY     down << 16 | RETROK code (libretro's, the X front end maps keysyms)
 *   MOUSE   dx (signed 12 bits) << 12 | dy (signed 12 bits)
 *   BUTTONS mouse buttons (bit 0 left, 1 right, 2 middle)
 *   JOY     port << 16 | RETRO_DEVICE_ID_JOYPAD_* bits (the whole state)
 */
enum { C1UA_EV_KEY = 1, C1UA_EV_MOUSE, C1UA_EV_BUTTONS, C1UA_EV_JOY };

/* c1bench.adf (files/bench): CoreMark's marks in chip RAM */
#define C1BENCH_MARK        0x0007FF00UL    /* "C1BS", "C1BE", "C1BD" */
#define C1BENCH_TEXT        0x0007E000UL    /* its report */

/* the config text's keys: "model=A500|A600|A1200", "var=uae4arm_xxx=value"
   (a core option), "cfg=<uae config line>" (kickstart_rom_file=<file name>,
   floppy0..3=<file name>, hardfile2=..., cpu_speed=...), "content=<file name>" */

#endif
