/*
 * c1q3_board.h -- z3660ARM_Core_q3: what core 1 (the whole of ioquake3) and
 * AMIX (c1q3, the 060's helper) share in the Z3660's board memory.  Offsets
 * are from the start of board memory: the ARM sees them at 0x18000000 +
 * offset, the 68k through /dev/zz9000 (mmap offset = board offset - 2 MB).
 * Words are big-endian (the 68k's order); the ARM swaps.
 *
 * The image runs alone on core 1, like uae4arm's (c1uae_board.h), and takes
 * the same areas: the player's control page, its packet ring as the data
 * window, the overlay's two buffers and the HDMI sound ring.  The rest is
 * bench.h's harness (control block HB_CTL_OFF, loader C1LD_OFF, -reload).
 *
 * Start-up (c1q3):
 *   1. armbench -load/-reload z3660ARM_Core_q3.bin, wait for C1Q3_MAGIC
 *   2. per file: CMD_OPEN (path in the text area, W_SIZE, W_FLAGS), then
 *      up to C1Q3_CHUNK_MAX bytes at a time into the window, W_FILE/W_OFF/
 *      W_LEN and CMD_CHUNK; core 1 copies it into its own memory
 *   3. CMD_START with the command line in the text area: the engine starts,
 *      state C1Q3_RUNNING
 *   4. while it runs: input events into the ring (68k writes, core 1 reads
 *      at every frame), CMD_EXEC for a console command, a few polls a
 *      second of W_DIRTY and W_LOG_HEAD by the 68k (each 68k read of board
 *      memory stalls core 0), CMD_FLUSH for a written file
 *   5. the game quits (or CMD_STOP): state C1Q3_QUIT, the 68k flushes what
 *      is left and reloads the image for the next run
 *
 * Files: paths as the engine sees them.  The front end puts the game data
 * under /base (fs_basepath) and the user's files under /home (fs_homepath);
 * only files under /home/ are writable, and only those come back.
 */
#ifndef C1Q3_BOARD_H
#define C1Q3_BOARD_H

#define C1Q3_OFF            0x07AFD000UL    /* the control page (ZZAV_OFF, as uae's C1UA page) */
#define C1Q3_MAGIC          0x43315133UL    /* "C1Q3": the core runs and listens */
#define C1Q3_VERSION        0x00000100UL    /* 0.1.0 of this protocol */

/* the data window: the player's packet ring (ZZAV_RING_OFF, 6 MB, uncached
   on both cores); its last 64 KB are core 1's log ring */
#define C1Q3_WIN_OFF        0x07100000UL
#define C1Q3_WIN_SIZE       0x00600000UL
#define C1Q3_CHUNK_MAX      0x005F0000UL
#define C1Q3_LOG_OFF        (C1Q3_WIN_OFF + C1Q3_CHUNK_MAX)
#define C1Q3_LOG_LEN        0x00010000UL
/* the overlay's two buffers, the HDMI ring (as c1uae_board.h) */
#define C1Q3_OVL0_OFF       0x06000000UL
#define C1Q3_OVL1_OFF       0x06800000UL
#define C1Q3_HDMI_OFF       0x07CE0000UL
#define C1Q3_HDMI_SIZE      30720UL
#define C1Q3_HDMI_POS_REG   0x7FC00128UL    /* the DMA's byte position in it (ARM address) */
#define C1Q3_HDMI_RATE      48000

/* W_STATE */
enum { C1Q3_IDLE, C1Q3_LOADING, C1Q3_RUNNING, C1Q3_QUIT, C1Q3_FAILED };

/* W_CMD, from the 68k; core 1 sets it back to 0 when done (W_RESULT) */
enum {
    C1Q3_CMD_NONE,
    C1Q3_CMD_OPEN,          /* text: the path; W_SIZE, W_FLAGS.  W_RESULT: the file's number */
    C1Q3_CMD_CHUNK,         /* W_LEN bytes of the window into file W_FILE at W_OFF */
    C1Q3_CMD_START,         /* text: the engine's command line */
    C1Q3_CMD_EXEC,          /* text: a console command (while running) */
    C1Q3_CMD_FLUSH,         /* the next written file: text = its path, W_SIZE its size
                               (C1Q3_REMOVED: deleted), W_OFF/W_LEN the piece in the
                               window; W_RESULT 1 more pieces of it follow, 0 the last,
                               -1 nothing written */
    C1Q3_CMD_STOP           /* quit now (as the console's quit) */
};
#define C1Q3_REMOVED        0xFFFFFFFFUL

#define C1Q3_FILE_RW        1               /* W_FLAGS: written back (under /home/ only) */

/* the 32-bit words of the page */
enum {
    C1Q3_W_MAGIC, C1Q3_W_VERSION, C1Q3_W_STATE, C1Q3_W_CMD, C1Q3_W_ARG, C1Q3_W_RESULT,
    C1Q3_W_FRAMES,              /* core 1: frames since START */
    C1Q3_W_FRAME_US,            /* core 1: the last frame, us */
    C1Q3_W_FRAME_US_AVG,        /* core 1: average over the last 32 */
    C1Q3_W_UNDERRUNS,           /* core 1: sound ran dry */
    C1Q3_W_CPU_HZ, C1Q3_W_TICK_HZ,
    C1Q3_W_ERR,                 /* core 1: why C1Q3_FAILED (C1Q3_E_*) */
    C1Q3_W_OWNER,               /* 68k: the pid of the c1q3 that runs the game (0: none) */
    C1Q3_W_IN_HEAD,             /* 68k: the next input event it writes */
    C1Q3_W_IN_TAIL,             /* core 1: the next input event it reads */
    C1Q3_W_LOG_HEAD,            /* core 1: bytes written into the log ring */
    C1Q3_W_FILE, C1Q3_W_OFF, C1Q3_W_LEN, C1Q3_W_SIZE, C1Q3_W_FLAGS,
    C1Q3_W_DIRTY,               /* core 1: files written and not flushed yet */
    C1Q3_W_OVL_PITCH, C1Q3_W_OVL_ROW0, C1Q3_W_OVL_MAXW, C1Q3_W_OVL_MAXH, /* 68k: where the picture goes */
    C1Q3_W_VOLUME,              /* 68k: 0-256 */
    C1Q3_W_HEAP_KB,             /* core 1: heap left (sbrk), KB */
    C1Q3_W_WIDTH, C1Q3_W_HEIGHT,/* core 1: the picture's size (from the renderer) */
    C1Q3_W_MOUSE,               /* core 1: 1 the game wants the mouse (grab), 0 free (menu, console) */
    C1Q3_W_FRAME_MS_MAX,        /* core 1: the longest frame of the last second, ms */
    C1Q3_W_SAMPLE = 40,         /* core 1, 4 times a second (private timer): count, then the
                                   interrupted pc, lr, sp, then code addresses on its stack */
    C1Q3_W_SAMPLE_END = 60,
    C1Q3_W_NWORDS = 64
};

enum { C1Q3_E_NONE, C1Q3_E_NOMEM, C1Q3_E_FILE, C1Q3_E_ENGINE, C1Q3_E_CORE };

/* byte offsets inside the page */
#define C1Q3_IN_OFF         0x400           /* input ring: C1Q3_IN_N events of 4 bytes */
#define C1Q3_IN_N           256
#define C1Q3_TEXT_OFF       0x800           /* paths, the command line, commands; NUL-ended */
#define C1Q3_TEXT_LEN       0x800

/*
 * Input events: one 32-bit word, type << 24 | data
 *   KEY     down << 16 | the engine's key number (K_*, keycodes.h; mouse
 *           buttons and the wheel are keys too: K_MOUSE1.., K_MWHEELUP/DOWN)
 *   CHAR    a character for the console and text fields (Latin-1)
 *   MOUSE   dx (signed 12 bits) << 12 | dy (signed 12 bits), relative
 *   FOCUS   1 the window has the keyboard, 0 lost it (all keys up)
 */
enum { C1Q3_EV_KEY = 1, C1Q3_EV_CHAR, C1Q3_EV_MOUSE, C1Q3_EV_FOCUS };

#endif
