/*
 * SDL.h -- sdl2-amix: the part of the SDL 2 API that pt2-clone uses,
 * implemented for AMIX (SVR4.0 on the Amiga 3000) on X11R6.3 and the
 * kernel's /dev/audio (sdl2amix.c).  Names and values follow SDL 2.0
 * where they are visible to programs; everything else is left out.
 *
 * No threads on AMIX: SDL_CreateThread runs the function to completion
 * before it returns, and the audio callback is driven from the program's
 * own calls (event polling, presenting, delays).
 */
#ifndef SDL_AMIX_SDL_H
#define SDL_AMIX_SDL_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 0
#define SDL_PATCHLEVEL    22
typedef struct SDL_version { uint8_t major, minor, patch; } SDL_version;
#define SDL_VERSION(x) do { (x)->major = SDL_MAJOR_VERSION; (x)->minor = SDL_MINOR_VERSION; (x)->patch = SDL_PATCHLEVEL; } while (0)
void SDL_GetVersion(SDL_version *ver);

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;
typedef uint8_t Uint8;
typedef int16_t Sint16;
typedef uint16_t Uint16;
typedef int32_t Sint32;
typedef uint32_t Uint32;
typedef uint64_t Uint64;

#define SDL_INIT_AUDIO    0x00000010u
#define SDL_INIT_VIDEO    0x00000020u
#define SDL_INIT_JOYSTICK 0x00000200u
int SDL_Init(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);
SDL_bool SDL_SetHint(const char *name, const char *value);
#define SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR "SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR"
#define SDL_free free
int SDL_setenv(const char *name, const char *value, int overwrite);
SDL_bool SDL_HasSSE(void);
SDL_bool SDL_HasSSE2(void);
void SDL_EnableScreenSaver(void);

/* time */
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
void SDL_Delay(Uint32 ms);

/* threads: run synchronously */
typedef struct SDL_Thread SDL_Thread;
typedef Sint32 (*SDL_ThreadFunction)(void *data);	/* int32_t: pt2's signature */
typedef enum { SDL_THREAD_PRIORITY_LOW, SDL_THREAD_PRIORITY_NORMAL, SDL_THREAD_PRIORITY_HIGH } SDL_ThreadPriority;
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void SDL_DetachThread(SDL_Thread *thread);
int SDL_SetThreadPriority(SDL_ThreadPriority priority);

/* video */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Cursor SDL_Cursor;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_DisplayMode { Uint32 format; int w, h, refresh_rate; void *driverdata; } SDL_DisplayMode;
typedef struct SDL_PixelFormat { Uint32 format; Uint8 BitsPerPixel, BytesPerPixel; } SDL_PixelFormat;
typedef struct SDL_Surface { Uint32 flags; SDL_PixelFormat *format; int w, h, pitch; void *pixels; } SDL_Surface;

#define SDL_WINDOW_FULLSCREEN         0x00000001u
#define SDL_WINDOW_SHOWN              0x00000004u
#define SDL_WINDOW_HIDDEN             0x00000008u
#define SDL_WINDOW_MINIMIZED          0x00000040u
#define SDL_WINDOW_INPUT_FOCUS        0x00000200u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000u)
#define SDL_WINDOW_ALLOW_HIGHDPI      0x00002000u
#define SDL_WINDOWPOS_CENTERED        0x2FFF0000u
#define SDL_RENDERER_PRESENTVSYNC     0x00000004u
#define SDL_PIXELFORMAT_ARGB8888      0x16362004u
#define SDL_TEXTUREACCESS_STREAMING   1
#define SDL_ALPHA_OPAQUE              255
typedef enum { SDL_BLENDMODE_NONE = 0 } SDL_BlendMode;

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *window);
void SDL_ShowWindow(SDL_Window *window);
void SDL_RaiseWindow(SDL_Window *window);
void SDL_RestoreWindow(SDL_Window *window);
void SDL_SetWindowTitle(SDL_Window *window, const char *title);
Uint32 SDL_GetWindowFlags(SDL_Window *window);
void SDL_GetWindowSize(SDL_Window *window, Sint32 *w, Sint32 *h);
void SDL_GetWindowPosition(SDL_Window *window, Sint32 *x, Sint32 *y);
int SDL_GetWindowDisplayIndex(SDL_Window *window);
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags);
int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode);
void SDL_GL_GetDrawableSize(SDL_Window *window, Sint32 *w, Sint32 *h);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
int SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer, SDL_BlendMode mode);
int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int SDL_RenderClear(SDL_Renderer *renderer);
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *src, const SDL_Rect *dst);
void SDL_RenderPresent(SDL_Renderer *renderer);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture *texture);
int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode mode);
int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth, Uint32 Rm, Uint32 Gm, Uint32 Bm, Uint32 Am);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
int SDL_SetSurfaceRLE(SDL_Surface *surface, int flag);
int SDL_SetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode mode);
SDL_Cursor *SDL_CreateColorCursor(SDL_Surface *surface, int hot_x, int hot_y);
SDL_Cursor *SDL_GetDefaultCursor(void);
void SDL_SetCursor(SDL_Cursor *cursor);
void SDL_FreeCursor(SDL_Cursor *cursor);
int SDL_ShowCursor(int toggle);

/* sdl2-amix extension: rows[y] != 0 marks texture rows drawn since the last
** present; present compares only those (and everything every 16th frame) */
void SDL_AMIX_SetDirtyRows(Uint8 *rows, int n);
void SDL_AMIX_SetNullAudio(int on);

#define SDL_MESSAGEBOX_ERROR 0x10
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window);

/* keyboard */
typedef enum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4,
    SDL_SCANCODE_B = 5,
    SDL_SCANCODE_C = 6,
    SDL_SCANCODE_D = 7,
    SDL_SCANCODE_E = 8,
    SDL_SCANCODE_F = 9,
    SDL_SCANCODE_G = 10,
    SDL_SCANCODE_H = 11,
    SDL_SCANCODE_I = 12,
    SDL_SCANCODE_J = 13,
    SDL_SCANCODE_K = 14,
    SDL_SCANCODE_L = 15,
    SDL_SCANCODE_M = 16,
    SDL_SCANCODE_N = 17,
    SDL_SCANCODE_O = 18,
    SDL_SCANCODE_P = 19,
    SDL_SCANCODE_Q = 20,
    SDL_SCANCODE_R = 21,
    SDL_SCANCODE_S = 22,
    SDL_SCANCODE_T = 23,
    SDL_SCANCODE_U = 24,
    SDL_SCANCODE_V = 25,
    SDL_SCANCODE_W = 26,
    SDL_SCANCODE_X = 27,
    SDL_SCANCODE_Y = 28,
    SDL_SCANCODE_Z = 29,
    SDL_SCANCODE_1 = 30,
    SDL_SCANCODE_2 = 31,
    SDL_SCANCODE_3 = 32,
    SDL_SCANCODE_4 = 33,
    SDL_SCANCODE_5 = 34,
    SDL_SCANCODE_6 = 35,
    SDL_SCANCODE_7 = 36,
    SDL_SCANCODE_8 = 37,
    SDL_SCANCODE_9 = 38,
    SDL_SCANCODE_0 = 39,
    SDL_SCANCODE_RETURN = 40,
    SDL_SCANCODE_ESCAPE = 41,
    SDL_SCANCODE_BACKSPACE = 42,
    SDL_SCANCODE_TAB = 43,
    SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_MINUS = 45,
    SDL_SCANCODE_EQUALS = 46,
    SDL_SCANCODE_LEFTBRACKET = 47,
    SDL_SCANCODE_RIGHTBRACKET = 48,
    SDL_SCANCODE_BACKSLASH = 49,
    SDL_SCANCODE_NONUSHASH = 50,
    SDL_SCANCODE_SEMICOLON = 51,
    SDL_SCANCODE_APOSTROPHE = 52,
    SDL_SCANCODE_GRAVE = 53,
    SDL_SCANCODE_COMMA = 54,
    SDL_SCANCODE_PERIOD = 55,
    SDL_SCANCODE_SLASH = 56,
    SDL_SCANCODE_CAPSLOCK = 57,
    SDL_SCANCODE_F1 = 58,
    SDL_SCANCODE_F2 = 59,
    SDL_SCANCODE_F3 = 60,
    SDL_SCANCODE_F4 = 61,
    SDL_SCANCODE_F5 = 62,
    SDL_SCANCODE_F6 = 63,
    SDL_SCANCODE_F7 = 64,
    SDL_SCANCODE_F8 = 65,
    SDL_SCANCODE_F9 = 66,
    SDL_SCANCODE_F10 = 67,
    SDL_SCANCODE_F11 = 68,
    SDL_SCANCODE_F12 = 69,
    SDL_SCANCODE_PRINTSCREEN = 70,
    SDL_SCANCODE_SCROLLLOCK = 71,
    SDL_SCANCODE_PAUSE = 72,
    SDL_SCANCODE_INSERT = 73,
    SDL_SCANCODE_HOME = 74,
    SDL_SCANCODE_PAGEUP = 75,
    SDL_SCANCODE_DELETE = 76,
    SDL_SCANCODE_END = 77,
    SDL_SCANCODE_PAGEDOWN = 78,
    SDL_SCANCODE_RIGHT = 79,
    SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_DOWN = 81,
    SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_NUMLOCKCLEAR = 83,
    SDL_SCANCODE_KP_DIVIDE = 84,
    SDL_SCANCODE_KP_MULTIPLY = 85,
    SDL_SCANCODE_KP_MINUS = 86,
    SDL_SCANCODE_KP_PLUS = 87,
    SDL_SCANCODE_KP_ENTER = 88,
    SDL_SCANCODE_KP_1 = 89,
    SDL_SCANCODE_KP_2 = 90,
    SDL_SCANCODE_KP_3 = 91,
    SDL_SCANCODE_KP_4 = 92,
    SDL_SCANCODE_KP_5 = 93,
    SDL_SCANCODE_KP_6 = 94,
    SDL_SCANCODE_KP_7 = 95,
    SDL_SCANCODE_KP_8 = 96,
    SDL_SCANCODE_KP_9 = 97,
    SDL_SCANCODE_KP_0 = 98,
    SDL_SCANCODE_KP_PERIOD = 99,
    SDL_SCANCODE_NONUSBACKSLASH = 100,
    SDL_SCANCODE_APPLICATION = 101,
    SDL_SCANCODE_MENU = 118,
    SDL_SCANCODE_LCTRL = 224,
    SDL_SCANCODE_LSHIFT = 225,
    SDL_SCANCODE_LALT = 226,
    SDL_SCANCODE_LGUI = 227,
    SDL_SCANCODE_RCTRL = 228,
    SDL_SCANCODE_RSHIFT = 229,
    SDL_SCANCODE_RALT = 230,
    SDL_SCANCODE_RGUI = 231,
    SDL_SCANCODE_MODE = 257,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;
typedef Sint32 SDL_Keycode;
#define SDLK_SCANCODE_MASK (1 << 30)
#define SDL_SCANCODE_TO_KEYCODE(x) ((x) | SDLK_SCANCODE_MASK)
enum {
    SDLK_UNKNOWN = 0, SDLK_RETURN = '\r', SDLK_ESCAPE = 27,
    SDLK_a = 'a', SDLK_c = 'c', SDLK_e = 'e', SDLK_n = 'n', SDLK_o = 'o', SDLK_r = 'r', SDLK_s = 's', SDLK_y = 'y',
    SDLK_KP_ENTER = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_KP_ENTER)
};
typedef enum {
    KMOD_NONE = 0, KMOD_LSHIFT = 0x0001, KMOD_RSHIFT = 0x0002, KMOD_LCTRL = 0x0040, KMOD_RCTRL = 0x0080,
    KMOD_LALT = 0x0100, KMOD_RALT = 0x0200, KMOD_LGUI = 0x0400, KMOD_RGUI = 0x0800,
    KMOD_NUM = 0x1000, KMOD_CAPS = 0x2000, KMOD_MODE = 0x4000
} SDL_Keymod;
typedef struct SDL_Keysym { SDL_Scancode scancode; SDL_Keycode sym; Uint16 mod; Uint32 unused; } SDL_Keysym;
SDL_Keymod SDL_GetModState(void);
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);

/* mouse */
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3
#define SDL_BUTTON(X)     (1 << ((X)-1))
#define SDL_BUTTON_LMASK  SDL_BUTTON(SDL_BUTTON_LEFT)
#define SDL_BUTTON_RMASK  SDL_BUTTON(SDL_BUTTON_RIGHT)
Uint32 SDL_GetMouseState(Sint32 *x, Sint32 *y);
Uint32 SDL_GetGlobalMouseState(Sint32 *x, Sint32 *y);
void SDL_WarpMouseInWindow(SDL_Window *window, int x, int y);

/* events */
typedef enum {
    SDL_QUIT = 0x100, SDL_WINDOWEVENT = 0x200, SDL_SYSWMEVENT,
    SDL_KEYDOWN = 0x300, SDL_KEYUP, SDL_TEXTEDITING, SDL_TEXTINPUT,
    SDL_MOUSEMOTION = 0x400, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_MOUSEWHEEL,
    SDL_DROPFILE = 0x1000
} SDL_EventType;
typedef enum {
    SDL_WINDOWEVENT_NONE, SDL_WINDOWEVENT_SHOWN, SDL_WINDOWEVENT_HIDDEN, SDL_WINDOWEVENT_EXPOSED,
    SDL_WINDOWEVENT_MOVED, SDL_WINDOWEVENT_RESIZED, SDL_WINDOWEVENT_SIZE_CHANGED, SDL_WINDOWEVENT_MINIMIZED,
    SDL_WINDOWEVENT_MAXIMIZED, SDL_WINDOWEVENT_RESTORED, SDL_WINDOWEVENT_ENTER, SDL_WINDOWEVENT_LEAVE,
    SDL_WINDOWEVENT_FOCUS_GAINED, SDL_WINDOWEVENT_FOCUS_LOST, SDL_WINDOWEVENT_CLOSE
} SDL_WindowEventID;
typedef struct SDL_SysWMmsg SDL_SysWMmsg;
typedef union SDL_Event {
    Uint32 type;
    struct { Uint32 type, timestamp, windowID; Uint8 event, padding1, padding2, padding3; Sint32 data1, data2; } window;
    struct { Uint32 type, timestamp, windowID; Uint8 state, repeat, padding2, padding3; SDL_Keysym keysym; } key;
    struct { Uint32 type, timestamp, windowID; char text[32]; } text;
    struct { Uint32 type, timestamp, windowID, which; Uint32 state; Sint32 x, y, xrel, yrel; } motion;
    struct { Uint32 type, timestamp, windowID, which; Uint8 button, state, clicks, padding1; Sint32 x, y; } button;
    struct { Uint32 type, timestamp, windowID, which; Sint32 x, y; Uint32 direction; } wheel;
    struct { Uint32 type, timestamp; char *file; Uint32 windowID; } drop;
    struct { Uint32 type, timestamp; SDL_SysWMmsg *msg; } syswm;
    Uint8 padding[56];
} SDL_Event;
#define SDL_DISABLE 0
#define SDL_ENABLE  1
int SDL_PollEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);
Uint8 SDL_EventState(Uint32 type, int state);

/* audio */
typedef Uint16 SDL_AudioFormat;
#define AUDIO_S16MSB 0x9010
#define AUDIO_S16    AUDIO_S16MSB	/* native on m68k */
typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);
typedef struct SDL_AudioSpec {
    int freq; SDL_AudioFormat format; Uint8 channels, silence; Uint16 samples, padding; Uint32 size;
    SDL_AudioCallback callback; void *userdata;
} SDL_AudioSpec;
typedef Uint32 SDL_AudioDeviceID;
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000001
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void SDL_LockAudioDevice(SDL_AudioDeviceID dev);
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);
int SDL_GetNumAudioDrivers(void);
const char *SDL_GetAudioDriver(int index);
int SDL_GetNumAudioDevices(int iscapture);
const char *SDL_GetAudioDeviceName(int index, int iscapture);

#ifdef __cplusplus
}
#endif
#endif
