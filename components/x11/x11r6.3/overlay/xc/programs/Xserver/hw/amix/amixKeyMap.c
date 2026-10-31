#include	"amix.h"
#include	"keysym.h"

/*
** MIN_AMIX_KEY
** Keycode of first key (tilde) pressed down.
**
** Keycodes 0x00 - 0x67 are codes of keys pressed.
*/
#define MIN_AMIX_KEY        1

/*
** MAX_AMIX_KEY
** Keycode of last key (Right AMIGA key) released.
**
** Keycodes 0x80 - 0xE7 are codes of keys released.
*/
#define MAX_AMIX_KEY        0xE8

#define AMIX_GLYPHS_PER_KEY 3

static KeySym AmixMap[] =
{
  	XK_quoteleft,	XK_asciitilde,	NoSymbol,	/* 0x00 */
	XK_1,		XK_exclam,	NoSymbol,	/* 0x01 */
	XK_2,		XK_at,		NoSymbol,	/* 0x02 */
	XK_3,		XK_numbersign,	NoSymbol,	/* 0x03 */
	XK_4,		XK_dollar,	NoSymbol,	/* 0x04 */
	XK_5,		XK_percent,	NoSymbol,	/* 0x05 */
	XK_6,		XK_asciicircum,	NoSymbol,	/* 0x06 */
	XK_7,		XK_ampersand,	NoSymbol,	/* 0x07 */
	XK_8,		XK_asterisk,	NoSymbol,	/* 0x08 */
	XK_9,		XK_parenleft,	NoSymbol,	/* 0x09 */
	XK_0,		XK_parenright,	NoSymbol,	/* 0x0a */
	XK_minus,	XK_underscore,	NoSymbol,	/* 0x0b */
	XK_equal,	XK_plus,	NoSymbol,	/* 0x0c */
	XK_backslash,	XK_bar,		NoSymbol,	/* 0x0d */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x0e */
	XK_KP_0,	NoSymbol,	XK_Insert,	/* 0x0f */

	XK_Q,		NoSymbol,	NoSymbol,	/* 0x10 */
	XK_W,		NoSymbol,	NoSymbol,	/* 0x11 */
	XK_E,		NoSymbol,	NoSymbol,	/* 0x12 */
	XK_R,		NoSymbol,	NoSymbol,	/* 0x13 */
	XK_T,		NoSymbol,	NoSymbol,	/* 0x14 */
	XK_Y,		NoSymbol,	NoSymbol,	/* 0x15 */
	XK_U,		NoSymbol,	NoSymbol,	/* 0x16 */
	XK_I,		NoSymbol,	NoSymbol,	/* 0x17 */
	XK_O,		NoSymbol,	NoSymbol,	/* 0x18 */
	XK_P,		NoSymbol,	NoSymbol,	/* 0x19 */
	XK_bracketleft,	XK_braceleft,	NoSymbol,	/* 0x1a */
	XK_bracketright,XK_braceright,	NoSymbol,	/* 0x1b */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x1c */
	XK_KP_1,	NoSymbol,	XK_End,		/* 0x1d */
	XK_KP_2,	NoSymbol,	XK_Down,	/* 0x1e */
	XK_KP_3,	NoSymbol,	XK_Next,	/* 0x1f */

	XK_A,		NoSymbol,	NoSymbol,	/* 0x20 */
	XK_S,		NoSymbol,	NoSymbol,	/* 0x21 */
	XK_D,		NoSymbol,	NoSymbol,	/* 0x22 */
	XK_F,		NoSymbol,	NoSymbol,	/* 0x23 */
	XK_G,		NoSymbol,	NoSymbol,	/* 0x24 */
	XK_H,		NoSymbol,	NoSymbol,	/* 0x25 */
	XK_J,		NoSymbol,	NoSymbol,	/* 0x26 */
	XK_K,		NoSymbol,	NoSymbol,	/* 0x27 */
	XK_L,		NoSymbol,	NoSymbol,	/* 0x28 */
	XK_semicolon,	XK_colon,	NoSymbol,	/* 0x29 */
	XK_quoteright,	XK_quotedbl,	NoSymbol,	/* 0x2a */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x2b */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x2c */
	XK_KP_4,	NoSymbol,	XK_Left,	/* 0x2d */
	XK_KP_5,	NoSymbol,	XK_Select,	/* 0x2e */
	XK_KP_6,	NoSymbol,	XK_Right,	/* 0x2f */

	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x30 */
	XK_Z,		NoSymbol,	NoSymbol,	/* 0x31 */
	XK_X,		NoSymbol,	NoSymbol,	/* 0x32 */
	XK_C,		NoSymbol,	NoSymbol,	/* 0x33 */
	XK_V,		NoSymbol,	NoSymbol,	/* 0x34 */
	XK_B,		NoSymbol,	NoSymbol,	/* 0x35 */
	XK_N,		NoSymbol,	NoSymbol,	/* 0x36 */
	XK_M,		NoSymbol,	NoSymbol,	/* 0x37 */
	XK_comma,	XK_less,	NoSymbol,	/* 0x38 */
	XK_period,	XK_greater,	NoSymbol,	/* 0x39 */
	XK_slash,	XK_question,	NoSymbol,	/* 0x3a */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x3b */
	XK_KP_Decimal,	NoSymbol,	XK_Delete,	/* 0x3c */
	XK_KP_7,	NoSymbol,	XK_Home,	/* 0x3d */
	XK_KP_8,	NoSymbol,	XK_Up,		/* 0x3e */
	XK_KP_9,	NoSymbol,	XK_Prior,	/* 0x3f */

	XK_space,	NoSymbol,	NoSymbol,	/* 0x40 */
	XK_BackSpace,	NoSymbol,	NoSymbol,	/* 0x41 */
	XK_Tab,		NoSymbol,	NoSymbol,	/* 0x42 */
	XK_KP_Enter,	NoSymbol,	NoSymbol,	/* 0x43 */
	XK_Return,	NoSymbol,	NoSymbol,	/* 0x44 */
	XK_Escape,	NoSymbol,	NoSymbol,	/* 0x45 */
	XK_Delete,	NoSymbol,	NoSymbol,	/* 0x46 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x47 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x48 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x49 */
	XK_KP_Subtract,	NoSymbol,	NoSymbol,	/* 0x4a */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x4b */
	XK_Up,		NoSymbol,	NoSymbol,	/* 0x4c */
	XK_Down,	NoSymbol,	NoSymbol,	/* 0x4d */
	XK_Right,	NoSymbol,	NoSymbol,	/* 0x4e */
	XK_Left,	NoSymbol,	NoSymbol,	/* 0x4f */

	XK_F1,		NoSymbol,	NoSymbol,	/* 0x50 */
	XK_F2,		NoSymbol,	NoSymbol,	/* 0x51 */
	XK_F3,		NoSymbol,	NoSymbol,	/* 0x52 */
	XK_F4,		NoSymbol,	NoSymbol,	/* 0x53 */
	XK_F5,		NoSymbol,	NoSymbol,	/* 0x54 */
	XK_F6,		NoSymbol,	NoSymbol,	/* 0x55 */
	XK_F7,		NoSymbol,	NoSymbol,	/* 0x56 */
	XK_F8,		NoSymbol,	NoSymbol,	/* 0x57 */
	XK_F9,		NoSymbol,	NoSymbol,	/* 0x58 */
	XK_F10,		NoSymbol,	NoSymbol,	/* 0x59 */
	XK_parenleft,	NoSymbol,	XK_Num_Lock,	/* 0x5a */
	XK_parenright,	NoSymbol,	XK_Scroll_Lock,	/* 0x5b */
	XK_KP_Divide,	NoSymbol,	NoSymbol,	/* 0x5c */
	XK_KP_Multiply,	NoSymbol,	XK_Print,	/* 0x5d */
	XK_KP_Add,	NoSymbol,	NoSymbol,	/* 0x5e */
	XK_Help,	NoSymbol,	NoSymbol,	/* 0x5f */

	XK_Shift_L,	NoSymbol,	NoSymbol,	/* 0x60 */
	XK_Shift_R,	NoSymbol,	NoSymbol,	/* 0x61 */
	XK_Caps_Lock,	NoSymbol,	NoSymbol,	/* 0x62 */
	XK_Control_L,	NoSymbol,	NoSymbol,	/* 0x63 */
	XK_Alt_L,	NoSymbol,	NoSymbol,	/* 0x64 */
	XK_Alt_R,	NoSymbol,	NoSymbol,	/* 0x65 */
	XK_Meta_L,	NoSymbol,	NoSymbol,	/* 0x66 */
	XK_Meta_R,	NoSymbol,	NoSymbol,	/* 0x67 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x68 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x69 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x6a */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x6b */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x6c */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x6d */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x6e */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x6f */

	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x70 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x71 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x72 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x73 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x74 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x75 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x76 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x77 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x78 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x79 */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x7a */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x7b */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x7c */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x7d */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x7e */
	NoSymbol,	NoSymbol,	NoSymbol,	/* 0x7f */
};

#define	cT	(ControlMask)
#define	sH	(ShiftMask)
#define	lK	(LockMask)
#define	m1	(Mod1Mask)		/* Alt-Key */
#define	m2	(Mod2Mask)		/* Amiga-Keys */

CARD8 amixModMap[MAP_LENGTH] =
{
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 00-0f */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 10-1f */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 20-2f */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 30-3f */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 40-4f */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 50-5f */
    0, sH, sH, lK, cT, m1, m1, m2, m2,  0,  0,  0,  0,  0,  0,  0, /* 60-6f */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 70-7f */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 80-8f */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 90-9f */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* a0-af */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* b0-bf */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* c0-cf */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* d0-df */
    0, sH, sH, lK, cT, m1, m1, m2, m2,  0,  0,  0,  0,  0,  0,  0, /* e0-ef */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* f0-ff */
};


KeySymsRec amixKeySyms =
{
    AmixMap, MIN_AMIX_KEY, MAX_AMIX_KEY, AMIX_GLYPHS_PER_KEY,
};
