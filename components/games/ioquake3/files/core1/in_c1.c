/*
 * in_c1.c -- ioquake3's keyboard and mouse on the Z3660's core 1, in place
 * of sdl/sdl_input.c: the 68k (c1q3) maps the X window's events to the
 * engine's key numbers and characters and writes them into the input ring
 * (c1q3_board.h); here they become engine events once a frame.
 *
 * The mouse is relative (the 68k grabs and warps the pointer); the game
 * asks for it except while the console is down or a map loads, as
 * amix_input.c (the 060 port's) does.
 */
#include "client/client.h"
#include "c1q3.h"
#include "c1q3_board.h"

void sys_c1_poll(void);

static cvar_t *in_mouse;
static int mouse_wanted = -1;

void IN_Init(void *windowData)
{
    (void)windowData;
    in_mouse = Cvar_Get("in_mouse", "1", CVAR_ARCHIVE);
    mouse_wanted = -1;
}

void IN_Shutdown(void)
{
    c1p_set_mouse(0);
    mouse_wanted = -1;
}

void IN_Restart(void)
{
    IN_Shutdown();
    IN_Init(NULL);
}

void IN_Frame(void)
{
    uint32_t ev;
    int grab, dx = 0, dy = 0;

    sys_c1_poll();
    while (c1p_next_event(&ev)) {
        const uint32_t d = ev & 0xFFFFFF;

        switch (ev >> 24) {
        case C1Q3_EV_KEY:
            if ((d & 0xFFFF) && (d & 0xFFFF) < MAX_KEYS)
                Com_QueueEvent(0, SE_KEY, (int)(d & 0xFFFF), (d >> 16) & 1, 0, NULL);
            break;
        case C1Q3_EV_CHAR:
            if (d)
                Com_QueueEvent(0, SE_CHAR, (int)d, 0, 0, NULL);
            break;
        case C1Q3_EV_MOUSE:
            dx += (int32_t)(d << 8) >> 20;
            dy += (int32_t)(d << 20) >> 20;
            break;
        case C1Q3_EV_FOCUS:
            if (!d)
                Key_ClearStates();
            Cvar_SetValue("com_unfocused", d ? 0 : 1);
            break;
        }
    }
    if ((dx || dy) && in_mouse->integer)
        Com_QueueEvent(0, SE_MOUSE, dx, dy, 0, NULL);
    /* as amix_input.c: free in the console and while a map loads */
    grab = in_mouse->integer && !(Key_GetCatcher() & KEYCATCH_CONSOLE) &&
           (clc.state == CA_DISCONNECTED || clc.state == CA_ACTIVE);
    if (grab != mouse_wanted) {
        mouse_wanted = grab;
        c1p_set_mouse(grab);
    }
}
