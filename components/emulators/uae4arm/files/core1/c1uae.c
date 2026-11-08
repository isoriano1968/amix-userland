/*
 * c1uae.c -- a minimal libretro frontend for upstream's uae4arm core, for
 * the Z3660's core 1 (bare metal) and its PC test under qemu-arm.  The
 * core's callbacks end in the platform (c1uae.h: c1p_*).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "libretro.h"
#include "c1uae.h"

uint8_t c1_key[512];
uint16_t c1_joy[2];
int16_t c1_mouse_dx, c1_mouse_dy;
uint8_t c1_mouse_btn;
uint32_t c1_frames;

#define MAX_VARS    32
#define MAX_LINES   64

static struct { char key[48], value[64]; } vars[MAX_VARS];
static int nvars;
static char *lines[MAX_LINES];
static int nlines;
static const char *sysdir = ".";
static int16_t mouse_x, mouse_y;        /* this frame's motion, as the core reads it */

void c1uae_set_var(const char *key, const char *value)
{
    int i;

    for (i = 0; i < nvars; i++)
        if (!strcmp(vars[i].key, key))
            break;
    if (i == MAX_VARS)
        return;
    if (i == nvars)
        nvars++;
    snprintf(vars[i].key, sizeof(vars[i].key), "%s", key);
    snprintf(vars[i].value, sizeof(vars[i].value), "%s", value);
}

void c1uae_add_line(const char *line)
{
    if (nlines < MAX_LINES)
        lines[nlines++] = strdup(line);
}

/* libretro-core.cpp (our patch): the lines, after the model's defaults */
const char *c1uae_config_line(int i)
{
    return i < nlines ? lines[i] : NULL;
}

static bool environ(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = (struct retro_variable *)data;
        int i;

        for (i = 0; i < nvars; i++)
            if (!strcmp(vars[i].key, v->key)) {
                v->value = vars[i].value;
                return true;
            }
        v->value = NULL;
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        *(const char **)data = sysdir;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
        const struct retro_message *m = (const struct retro_message *)data;

        c1p_log(m->msg);
        c1p_log("\n");
        return true;
    }
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        *(unsigned *)data = 0;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SHUTDOWN:
        return true;
    default:
        return false;
    }
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (data)
        c1p_video((const uint16_t *)data, w, h, pitch);
}

static void audio1(int16_t l, int16_t r)
{
    int16_t s[2] = { l, r };

    c1p_audio(s, 1);
}

static size_t audio(const int16_t *data, size_t frames)
{
    c1p_audio(data, frames);
    return frames;
}

static void input_poll(void)
{
    c1p_input();
    mouse_x = c1_mouse_dx;
    mouse_y = c1_mouse_dy;
    c1_mouse_dx = c1_mouse_dy = 0;
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    switch (device & RETRO_DEVICE_MASK) {
    case RETRO_DEVICE_KEYBOARD:
        return port == 0 && id < 512 ? c1_key[id] : 0;
    case RETRO_DEVICE_JOYPAD:
        return port < 2 && id < 16 ? (c1_joy[port] >> id) & 1 : 0;
    case RETRO_DEVICE_MOUSE:
        if (port)
            return 0;
        switch (id) {
        case RETRO_DEVICE_ID_MOUSE_X: return mouse_x;
        case RETRO_DEVICE_ID_MOUSE_Y: return mouse_y;
        case RETRO_DEVICE_ID_MOUSE_LEFT: return c1_mouse_btn & 1;
        case RETRO_DEVICE_ID_MOUSE_RIGHT: return (c1_mouse_btn >> 1) & 1;
        case RETRO_DEVICE_ID_MOUSE_MIDDLE: return (c1_mouse_btn >> 2) & 1;
        }
        return 0;
    }
    return 0;
}

int c1uae_start(const char *dir, const char *content)
{
    struct retro_game_info gi;
    char rate[32];

    sysdir = dir;
    snprintf(rate, sizeof(rate), "sound_frequency=%d", C1UAE_RATE);
    c1uae_add_line(rate);
    retro_set_environment(environ);
    retro_set_video_refresh(video);
    retro_set_audio_sample(audio1);
    retro_set_audio_sample_batch(audio);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();
    memset(&gi, 0, sizeof(gi));
    gi.path = content;
    return retro_load_game(&gi) ? 0 : -1;
}

void c1uae_frame(void)
{
    retro_run();
    c1_run_workers();
    c1_frames++;
}
