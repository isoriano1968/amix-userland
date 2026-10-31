/*
 * amix_input.c -- ioquake3's keyboard and mouse on AMIX, from the X events
 * of the GL window (amix_glimp.c), in place of sdl/sdl_input.c (2026-10-27).
 *
 * Keys go to the engine as SE_KEY (and printable ones also as SE_CHAR); the
 * console key is ` ~ and, on a German keyboard, ^ (dead_circumflex).  In the
 * game the pointer is grabbed and hidden and every motion is measured from
 * the window's centre, where it is warped back (relative mouse); in the
 * console, while loading and without focus it is free.  No joystick.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "../code/client/client.h"
#include "../code/sys/sys_local.h"

extern Display *amix_dpy;
extern Window amix_win;

static cvar_t *in_mouse, *in_nograb;
static qboolean mouse_active, focused = qtrue;
static int in_eventTime;
static Cursor blank;

static int key_for(KeySym k)
{
    switch (k) {
    case XK_Escape:     return K_ESCAPE;
    case XK_Return:     return K_ENTER;
    case XK_KP_Enter:   return K_KP_ENTER;
    case XK_Tab:        return K_TAB;
    case XK_BackSpace:  return K_BACKSPACE;
    case XK_space:      return K_SPACE;
    case XK_Up:         return K_UPARROW;
    case XK_Down:       return K_DOWNARROW;
    case XK_Left:       return K_LEFTARROW;
    case XK_Right:      return K_RIGHTARROW;
    case XK_Shift_L: case XK_Shift_R:       return K_SHIFT;
    case XK_Control_L: case XK_Control_R:   return K_CTRL;
    case XK_Alt_L: case XK_Alt_R: case XK_Meta_L: case XK_Meta_R: return K_ALT;
    case XK_Insert:     return K_INS;
    case XK_Delete:     return K_DEL;
    case XK_Home:       return K_HOME;
    case XK_End:        return K_END;
    case XK_Prior:      return K_PGUP;
    case XK_Next:       return K_PGDN;
    case XK_Pause:      return K_PAUSE;
    case XK_KP_Home:    return K_KP_HOME;
    case XK_KP_Up:      return K_KP_UPARROW;
    case XK_KP_Prior:   return K_KP_PGUP;
    case XK_KP_Left:    return K_KP_LEFTARROW;
    case XK_KP_Begin:   return K_KP_5;
    case XK_KP_Right:   return K_KP_RIGHTARROW;
    case XK_KP_End:     return K_KP_END;
    case XK_KP_Down:    return K_KP_DOWNARROW;
    case XK_KP_Next:    return K_KP_PGDN;
    case XK_KP_Insert:  return K_KP_INS;
    case XK_KP_Delete:  return K_KP_DEL;
    case XK_KP_Add:     return K_KP_PLUS;
    case XK_KP_Subtract: return K_KP_MINUS;
    case XK_KP_Divide:  return K_KP_SLASH;
    case XK_KP_Multiply: return K_KP_STAR;
    case XK_grave: case XK_asciitilde: case XK_asciicircum: case XK_dead_circumflex:
                        return K_CONSOLE;
    }
    if (k >= XK_F1 && k <= XK_F12)
        return K_F1 + (int) (k - XK_F1);
    if (k >= XK_A && k <= XK_Z)
        return (int) (k - XK_A + 'a');
    if (k >= 32 && k < 127)
        return (int) k;
    return 0;
}

static void mouse_on(void)
{
    if (mouse_active || !in_mouse->integer || in_nograb->integer)
        return;
    if (!blank) {
        Pixmap p = XCreatePixmap(amix_dpy, amix_win, 1, 1, 1);
        XColor c;
        memset(&c, 0, sizeof c);
        blank = XCreatePixmapCursor(amix_dpy, p, p, &c, &c, 0, 0);
        XFreePixmap(amix_dpy, p);
    }
    XDefineCursor(amix_dpy, amix_win, blank);
    if (XGrabPointer(amix_dpy, amix_win, True,
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, amix_win, None, CurrentTime) != GrabSuccess)
        return;
    XWarpPointer(amix_dpy, None, amix_win, 0, 0, 0, 0, cls.glconfig.vidWidth / 2, cls.glconfig.vidHeight / 2);
    mouse_active = qtrue;
}

static void mouse_off(void)
{
    if (!mouse_active)
        return;
    XUngrabPointer(amix_dpy, CurrentTime);
    XUndefineCursor(amix_dpy, amix_win);
    mouse_active = qfalse;
}

static void events(void)
{
    XEvent ev;
    KeySym k;
    char buf[16];
    int n, key, i, cx = cls.glconfig.vidWidth / 2, cy = cls.glconfig.vidHeight / 2;

    while (XPending(amix_dpy)) {
        XNextEvent(amix_dpy, &ev);
        switch (ev.type) {
        case KeyPress:
        case KeyRelease:
            /* a release followed at once by a press of the same key is the
               server's autorepeat: keep the key down, repeat the character */
            if (ev.type == KeyRelease && XPending(amix_dpy)) {
                XEvent next;
                XPeekEvent(amix_dpy, &next);
                if (next.type == KeyPress && next.xkey.keycode == ev.xkey.keycode &&
                    next.xkey.time == ev.xkey.time) {
                    XNextEvent(amix_dpy, &next);
                    n = XLookupString(&next.xkey, buf, sizeof buf, &k, NULL);
                    key = key_for(k);
                    if (key)
                        Com_QueueEvent(in_eventTime, SE_KEY, key, qtrue, 0, NULL);
                    for (i = 0; i < n; i++)
                        if ((unsigned char) buf[i] >= 32 && buf[i] != 127)
                            Com_QueueEvent(in_eventTime, SE_CHAR, (unsigned char) buf[i], 0, 0, NULL);
                    break;
                }
            }
            n = XLookupString(&ev.xkey, buf, sizeof buf, &k, NULL);
            key = key_for(k);
            if (key)
                Com_QueueEvent(in_eventTime, SE_KEY, key, ev.type == KeyPress, 0, NULL);
            if (ev.type == KeyPress && key != K_CONSOLE)
                for (i = 0; i < n; i++)
                    if ((unsigned char) buf[i] >= 32 && buf[i] != 127)
                        Com_QueueEvent(in_eventTime, SE_CHAR, (unsigned char) buf[i], 0, 0, NULL);
            break;
        case ButtonPress:
        case ButtonRelease:
            switch (ev.xbutton.button) {
            case 1: key = K_MOUSE1; break;
            case 2: key = K_MOUSE3; break;
            case 3: key = K_MOUSE2; break;
            case 4: key = K_MWHEELUP; break;
            case 5: key = K_MWHEELDOWN; break;
            default: key = 0; break;
            }
            if (key)
                Com_QueueEvent(in_eventTime, SE_KEY, key, ev.type == ButtonPress, 0, NULL);
            break;
        case MotionNotify:
            if (mouse_active) {
                int dx = ev.xmotion.x - cx, dy = ev.xmotion.y - cy;
                if (dx || dy) {
                    Com_QueueEvent(in_eventTime, SE_MOUSE, dx, dy, 0, NULL);
                    XWarpPointer(amix_dpy, None, amix_win, 0, 0, 0, 0, cx, cy);
                }
            }
            break;
        case FocusIn:
            focused = qtrue;
            Cvar_SetValue("com_unfocused", 0);
            break;
        case FocusOut:
            focused = qfalse;
            Cvar_SetValue("com_unfocused", 1);
            break;
        case ClientMessage:
            Cbuf_ExecuteText(EXEC_APPEND, "quit\n");
            break;
        }
    }
}

void IN_Frame(void)
{
    qboolean loading = clc.state != CA_DISCONNECTED && clc.state != CA_ACTIVE;

    if (!amix_dpy)
        return;
    if ((Key_GetCatcher() & KEYCATCH_CONSOLE) || loading || !focused)
        mouse_off();
    else
        mouse_on();
    events();
    in_eventTime = Sys_Milliseconds();
}

void IN_Init(void *windowData)
{
    (void) windowData;
    Com_DPrintf("\n------- Input Initialization (X11) -------\n");
    in_mouse = Cvar_Get("in_mouse", "1", CVAR_ARCHIVE);
    in_nograb = Cvar_Get("in_nograb", "0", CVAR_ARCHIVE);
    Cvar_Get("in_keyboardDebug", "0", CVAR_ARCHIVE);
    Cvar_Get("in_joystick", "0", CVAR_ARCHIVE | CVAR_LATCH);
    Cvar_SetValue("com_unfocused", 0);
    Cvar_SetValue("com_minimized", 0);
    mouse_active = qfalse;
}

void IN_Shutdown(void)
{
    if (amix_dpy)
        mouse_off();
    if (blank && amix_dpy)
        XFreeCursor(amix_dpy, blank);
    blank = 0;
}

void IN_Restart(void)
{
    IN_Shutdown();
    IN_Init(NULL);
}
