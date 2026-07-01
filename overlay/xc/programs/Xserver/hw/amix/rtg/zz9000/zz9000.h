#ifndef ZZ9000_H
#define ZZ9000_H

#include "../rtg.h"

#define ZZ9000_DEV          "/dev/zz9000"
#define ZZ9000_BPP          16
#define ZZ9000_DEPTH        16
#define ZZ9000_BYTES_PER_PIXEL 2
#define ZZ9000_FB_OFFSET    0x10000

#define ZZ9K_COLOR_16BIT565 1
#define ZZ9K_SWITCH_CAPTURE 0
#define ZZ9K_SWITCH_RTG     1

#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_SETMODE     (ZZ9K_IOC | 2)
#define ZZ9KIOC_SETSWITCH   (ZZ9K_IOC | 4)
#define ZZ9KIOC_FILL        (ZZ9K_IOC | 5)
#define ZZ9KIOC_PROBE       (ZZ9K_IOC | 9)
#define ZZ9KIOC_GETFBINFO   (ZZ9K_IOC | 11)
#define ZZ9KIOC_FILLRECT    (ZZ9K_IOC | 12)
#define ZZ9KIOC_COPYRECT    (ZZ9K_IOC | 13)

struct _ZZ9000ModeRec {
    char *name;
    int   w;
    int   h;
};

typedef struct _ZZ9000ModeRec ZZ9000ModeRec;
typedef struct _ZZ9000ModeRec *ZZ9000ModePtr;

extern ZZ9000ModePtr ZZ9000_selected_mode;

#define ZZ9000_RED_MASK     0xF800
#define ZZ9000_GREEN_MASK   0x07E0
#define ZZ9000_BLUE_MASK    0x001F
#define ZZ9000_RED_SHIFT    11
#define ZZ9000_GREEN_SHIFT  5
#define ZZ9000_BLUE_SHIFT   0

#define ZZ9000_PIXEL(r, g, b) \
    ( (unsigned short)(((r) & 0xF8) << 8) | \
      (unsigned short)(((g) & 0xFC) << 3) | \
      (unsigned short)( (b)         >> 3) )

#define ZZ9000_PIXEL_X11(r, g, b) \
    ( (unsigned short)(((r) >> 8) & 0xF8) << 8 | \
      (unsigned short)(((g) >> 8) & 0xFC) << 3 | \
      (unsigned short)(((b) >> 8)        ) >> 3 )

struct zz9k_info {
    unsigned long base;
    unsigned long board_size;
    unsigned long fb_offset;
    unsigned long fb_size;
    unsigned short hw_version;
    unsigned short fw_version;
    unsigned short product;
    unsigned short zorro;
    unsigned short width;
    unsigned short height;
    unsigned short color_mode;
    unsigned short mode;
};

struct zz9k_fbinfo {
    unsigned long fb_offset;
    unsigned long fb_size;
    unsigned short width;
    unsigned short height;
    unsigned short color_mode;
    unsigned short depth;
    unsigned short bytes_per_pixel;
    unsigned short stride;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    unsigned long alpha_mask;
    unsigned short red_shift;
    unsigned short green_shift;
    unsigned short blue_shift;
    unsigned short alpha_shift;
};

struct zz9k_mode {
    unsigned short width;
    unsigned short height;
    unsigned short color_mode;
    unsigned short scale_mode;
};

struct zz9k_fill {
    unsigned long offset;
    unsigned long bytes;
    unsigned long value;
};

struct zz9k_fillrect {
    unsigned short x;
    unsigned short y;
    unsigned short width;
    unsigned short height;
    unsigned long color;
};

struct zz9k_copyrect {
    unsigned short src_x;
    unsigned short src_y;
    unsigned short dst_x;
    unsigned short dst_y;
    unsigned short width;
    unsigned short height;
};

struct _ZZ9000ScreenRec {
    int             fd;
    pointer         mapBase;
    unsigned long   mapSize;
    unsigned long   fbOffset;
    unsigned long   fbSize;
    unsigned short *fbBase;
};

typedef struct _ZZ9000ScreenRec ZZ9000ScreenRec;
typedef struct _ZZ9000ScreenRec *ZZ9000ScreenPtr;

#define GetZZ9000Screen(rtgp) \
    ((ZZ9000ScreenPtr)(rtgp)->cardPrivate)

Bool    ZZ9000Probe();
Bool    ZZ9000InitHW();
void    ZZ9000CloseHW();
Bool    ZZ9000SetMode();

Bool    ZZ9000ScreenInit();
Bool    ZZ9000CloseScreen();
void    ZZ9000DrawGuarantee();
Bool    ZZ9000CreateGC();
void    ZZ9000ValidateGC();
void    ZZ9000DestroyGC();
void    ZZ9000ChangeClip();
void    ZZ9000CopyClip();
void    ZZ9000DestroyClip();
void    ZZ9000QueryBestSize();

void    ZZ9000GetImage();
void    ZZ9000FillSpans();
void    ZZ9000SetSpans();
void    ZZ9000GetSpans();
void    ZZ9000SolidRect();
void    ZZ9000CopyWindow();
void    ZZ9000PaintWindow();
RegionPtr ZZ9000CopyArea();
RegionPtr ZZ9000CopyPlane();
void    ZZ9000PolyPoint();
void    ZZ9000ZeroLine();
void    ZZ9000PolySegment();
void    ZZ9000PolyRectangle();
void    ZZ9000FillPolygon();
void    ZZ9000PolyFillRect();
void    ZZ9000PolyText8();
void    ZZ9000ImageText8();
void    ZZ9000PutImage();
void    ZZ9000PushPixels();

Bool    ZZ9000CreateWindow();
Bool    ZZ9000DestroyWindow();
Bool    ZZ9000PositionWindow();
Bool    ZZ9000ChangeWindowAttributes();
Bool    ZZ9000RealizeWindow();
Bool    ZZ9000UnrealizeWindow();

Bool    ZZ9000CreateColormap();
void    ZZ9000DestroyColormap();
void    ZZ9000InstallColormap();
void    ZZ9000UninstallColormap();
int     ZZ9000ListInstalledColormaps();
void    ZZ9000StoreColors();
void    ZZ9000ResolveColor();
Bool    ZZ9000CreateDefColormap();

Bool    ZZ9000RealizeCursor();
Bool    ZZ9000UnrealizeCursor();
Bool    ZZ9000DisplayCursor();
Bool    ZZ9000SetCursorPosition();
void    ZZ9000CursorLimits();

#endif /* ZZ9000_H */
