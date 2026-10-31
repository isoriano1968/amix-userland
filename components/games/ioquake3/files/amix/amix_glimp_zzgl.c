/*
 * amix_glimp_zzgl.c -- ioquake3's GL window on AMIX with the GL drawn by the
 * Z3660's second ARM core (z3660gl-amix's zzgl): in place of amix_glimp.c
 * when built with AMIX_ZZGL (2026-10-30).
 *
 * The X window only takes the input and holds the place: core 1 renders
 * with Mesa 18.3 and shows each frame in the RTG overlay over the window.
 * GL functions by name from zzgl (zzgl_GetProcAddress).  The same file
 * builds for the PC test, where zzgl_host.c runs core 1's side in-process
 * and draws into the window.
 */
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "../code/renderercommon/tr_common.h"
#include "../code/sys/sys_local.h"
#include "zzgl_enc.h"
#include "zzgl_platform.h"

Display *amix_dpy;              /* for amix_input.c */
Window amix_win;
static Atom amix_wm_delete;
static int amix_x, amix_y;

cvar_t *r_allowSoftwareGL;
cvar_t *r_allowResize;
cvar_t *r_centerWindow;
cvar_t *r_sdlDriver;
cvar_t *r_preferOpenGLES;

int qglMajorVersion, qglMinorVersion;
int qglesMajorVersion, qglesMinorVersion;

void (APIENTRYP qglActiveTextureARB) (GLenum texture);
void (APIENTRYP qglClientActiveTextureARB) (GLenum texture);
void (APIENTRYP qglMultiTexCoord2fARB) (GLenum target, GLfloat s, GLfloat t);
void (APIENTRYP qglLockArraysEXT) (GLint first, GLsizei count);
void (APIENTRYP qglUnlockArraysEXT) (void);

#define GLE(ret, name, ...) name##proc * qgl##name = NULL;
QGL_1_1_PROCS;
QGL_1_1_FIXED_FUNCTION_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
QGL_ES_1_1_PROCS;
QGL_ES_1_1_FIXED_FUNCTION_PROCS;
QGL_1_3_PROCS;
QGL_1_5_PROCS;
QGL_2_0_PROCS;
QGL_3_0_PROCS;
QGL_ARB_occlusion_query_PROCS;
QGL_ARB_framebuffer_object_PROCS;
QGL_ARB_vertex_array_object_PROCS;
QGL_EXT_direct_state_access_PROCS;
#undef GLE

static qboolean extension(const char *name)
{
    const char *e = glConfig.extensions_string;
    size_t n = strlen(name);

    while ((e = strstr(e, name)) != NULL) {
        if ((e == glConfig.extensions_string || e[-1] == ' ') && (e[n] == ' ' || e[n] == 0))
            return qtrue;
        e += n;
    }
    return qfalse;
}

static qboolean get_procs(void)
{
    qboolean ok = qtrue;
    const char *version;

#define GLE(ret, name, ...) qgl##name = (name##proc *) zzgl_GetProcAddress("gl" #name); \
    if (qgl##name == NULL) { \
        ri.Printf(PRINT_ALL, "ERROR: Missing OpenGL function %s\n", "gl" #name); \
        ok = qfalse; \
    }
    GLE(const GLubyte *, GetString, GLenum name)
    version = (const char *) qglGetString(GL_VERSION);
    if (!version)
        Com_Error(ERR_FATAL, "GL_VERSION is NULL");
    sscanf(version, "%d.%d", &qglMajorVersion, &qglMinorVersion);
    /* the renderer is GL 1.x: what it may use */
    if (qglMajorVersion > 1)
        qglMajorVersion = 1, qglMinorVersion = 5;
    QGL_1_1_PROCS;
    QGL_1_1_FIXED_FUNCTION_PROCS;
    QGL_DESKTOP_1_1_PROCS;
    QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
#undef GLE
    return ok;
}

void GLimp_Shutdown(void)
{
    ri.IN_Shutdown();
    zzgl_platform_close();
    if (amix_dpy) {
        if (amix_win)
            XDestroyWindow(amix_dpy, amix_win);
        XCloseDisplay(amix_dpy);
    }
    amix_dpy = NULL;
    amix_win = 0;
}

void GLimp_Minimize(void)
{
    if (amix_dpy && amix_win)
        XIconifyWindow(amix_dpy, amix_win, DefaultScreen(amix_dpy));
}

void GLimp_LogComment(char *comment)
{
    (void) comment;
}

void GLimp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256])
{
    (void) red; (void) green; (void) blue;      /* no hardware gamma */
}

/* the window's top left on the screen */
static void where(int *x, int *y)
{
    Window child;

    if (!XTranslateCoordinates(amix_dpy, amix_win, RootWindow(amix_dpy, DefaultScreen(amix_dpy)),
                               0, 0, x, y, &child))
        *x = *y = 0;
}

static qboolean set_mode(void)
{
    XSetWindowAttributes swa;
    XSizeHints hints;
    XEvent ev;
    int scr;

    if (!R_GetModeInfo(&glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, r_mode->integer)) {
        glConfig.vidWidth = 320;
        glConfig.vidHeight = 240;
        glConfig.windowAspect = 4.0f / 3.0f;
    }
    ri.Printf(PRINT_ALL, "...setting mode %d: %d %d\n", r_mode->integer,
              glConfig.vidWidth, glConfig.vidHeight);

    amix_dpy = XOpenDisplay(NULL);
    if (!amix_dpy) {
        ri.Printf(PRINT_ALL, "...cannot open the X display\n");
        return qfalse;
    }
    scr = DefaultScreen(amix_dpy);
    swa.border_pixel = 0;
    swa.background_pixel = BlackPixel(amix_dpy, scr);
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask | FocusChangeMask | ExposureMask;
    amix_win = XCreateWindow(amix_dpy, RootWindow(amix_dpy, scr), 0, 0,
                             glConfig.vidWidth, glConfig.vidHeight, 0, CopyFromParent, InputOutput,
                             CopyFromParent, CWBorderPixel | CWBackPixel | CWEventMask, &swa);
    hints.flags = PSize | PMinSize | PMaxSize;
    hints.width = hints.min_width = hints.max_width = glConfig.vidWidth;
    hints.height = hints.min_height = hints.max_height = glConfig.vidHeight;
    XSetStandardProperties(amix_dpy, amix_win, CLIENT_WINDOW_TITLE, CLIENT_WINDOW_MIN_TITLE,
                           None, NULL, 0, &hints);
    amix_wm_delete = XInternAtom(amix_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(amix_dpy, amix_win, &amix_wm_delete, 1);
    XMapWindow(amix_dpy, amix_win);
    do
        XNextEvent(amix_dpy, &ev);
    while (ev.type != MapNotify);
    where(&amix_x, &amix_y);

    if (zzgl_platform_open(amix_dpy, amix_win, amix_x, amix_y, glConfig.vidWidth, glConfig.vidHeight) < 0) {
        ri.Printf(PRINT_ALL, "...no GL on the Z3660's core 1\n");
        return qfalse;
    }
    glConfig.colorBits = 24;
    glConfig.depthBits = 16;
    glConfig.stencilBits = 0;
    glConfig.isFullscreen = qfalse;
    glConfig.stereoEnabled = qfalse;
    glConfig.displayFrequency = 60;

    if (!get_procs()) {
        ri.Printf(PRINT_ALL, "...the GL lacks functions the renderer needs\n");
        return qfalse;
    }
    return qtrue;
}

static void init_extensions(void)
{
    glConfig.textureCompression = TC_NONE;
    glConfig.textureEnvAddAvailable = qfalse;
    if (extension("GL_EXT_texture_env_add") && r_ext_texture_env_add->integer) {
        glConfig.textureEnvAddAvailable = qtrue;
        ri.Printf(PRINT_ALL, "...using GL_EXT_texture_env_add\n");
    }

    qglMultiTexCoord2fARB = NULL;
    qglActiveTextureARB = NULL;
    qglClientActiveTextureARB = NULL;
    if (extension("GL_ARB_multitexture") && r_ext_multitexture->value) {
        GLint units = 0;
        qglMultiTexCoord2fARB = zzgl_GetProcAddress("glMultiTexCoord2fARB");
        qglActiveTextureARB = zzgl_GetProcAddress("glActiveTextureARB");
        qglClientActiveTextureARB = zzgl_GetProcAddress("glClientActiveTextureARB");
        qglGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &units);
        glConfig.numTextureUnits = (int) units;
        if (qglActiveTextureARB && units > 1)
            ri.Printf(PRINT_ALL, "...using GL_ARB_multitexture\n");
        else
            qglMultiTexCoord2fARB = NULL, qglActiveTextureARB = NULL, qglClientActiveTextureARB = NULL;
    }

    qglLockArraysEXT = NULL;
    qglUnlockArraysEXT = NULL;
    if (extension("GL_EXT_compiled_vertex_array") && r_ext_compiled_vertex_array->value) {
        qglLockArraysEXT = zzgl_GetProcAddress("glLockArraysEXT");
        qglUnlockArraysEXT = zzgl_GetProcAddress("glUnlockArraysEXT");
        if (qglLockArraysEXT && qglUnlockArraysEXT)
            ri.Printf(PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n");
        else
            qglLockArraysEXT = NULL, qglUnlockArraysEXT = NULL;
    }

    textureFilterAnisotropic = qfalse;
    haveClampToEdge = qtrue;
}

void GLimp_Init(qboolean fixedFunction)
{
    (void) fixedFunction;
    r_allowSoftwareGL = ri.Cvar_Get("r_allowSoftwareGL", "1", CVAR_LATCH);
    r_sdlDriver = ri.Cvar_Get("r_sdlDriver", "x11", CVAR_ROM);
    r_allowResize = ri.Cvar_Get("r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH);
    r_centerWindow = ri.Cvar_Get("r_centerWindow", "0", CVAR_ARCHIVE | CVAR_LATCH);
    r_preferOpenGLES = ri.Cvar_Get("r_preferOpenGLES", "0", CVAR_ARCHIVE | CVAR_LATCH);

    ri.Sys_GLimpInit();
    if (!set_mode())
        ri.Error(ERR_FATAL, "GLimp_Init() - could not open the GL window");

    glConfig.driverType = GLDRV_ICD;
    glConfig.hardwareType = GLHW_GENERIC;
    glConfig.deviceSupportsGamma = qfalse;
    Q_strncpyz(glConfig.vendor_string, (char *) qglGetString(GL_VENDOR), sizeof glConfig.vendor_string);
    Q_strncpyz(glConfig.renderer_string, (char *) qglGetString(GL_RENDERER), sizeof glConfig.renderer_string);
    Q_strncpyz(glConfig.version_string, (char *) qglGetString(GL_VERSION), sizeof glConfig.version_string);
    Q_strncpyz(glConfig.extensions_string, (char *) qglGetString(GL_EXTENSIONS),
               sizeof glConfig.extensions_string);
    init_extensions();
    ri.Cvar_Get("r_availableModes", "", CVAR_ROM);
    ri.IN_Init(NULL);
}

void GLimp_EndFrame(void)
{
    static int n;

    zzgl_swap();
    zzgl_platform_frame();
    /* follow the window (cheaply: a round trip to the server now and then) */
    if (++n % 15 == 0) {
        int x, y;

        where(&x, &y);
        if (x != amix_x || y != amix_y) {
            amix_x = x;
            amix_y = y;
            zzgl_platform_place(x, y);
        }
    }
}
