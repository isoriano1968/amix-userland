/*
 * amix_glimp.c -- ioquake3's GL window on AMIX (X11R6.3 + Mesa 3.1's GLX),
 * in place of sdl/sdl_glimp.c and sdl/sdl_gamma.c (2026-10-27).
 *
 * One X window of r_mode's size (r_mode -1: r_customwidth x r_customheight),
 * an RGBA double-buffered visual with a depth buffer, Mesa statically
 * linked: GetProcAddress is a table (amix_glprocs.c).  On soft3D (the default)
 * Mesa draws on the Z3660's ARM, which does one texture per triangle, so
 * multitexture is not offered then (the renderer draws lightmaps in a
 * second pass).  No hardware gamma, no fullscreen mode switching.
 */
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>

#include "../code/renderercommon/tr_common.h"
#include "../code/sys/sys_local.h"

extern void *amix_GetProcAddress(const char *name);

Display *amix_dpy;              /* for amix_input.c */
Window amix_win;
static GLXContext amix_ctx;
static Atom amix_wm_delete;

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

#define GLE(ret, name, ...) qgl##name = (name##proc *) amix_GetProcAddress("gl" #name); \
    if (qgl##name == NULL) { \
        ri.Printf(PRINT_ALL, "ERROR: Missing OpenGL function %s\n", "gl" #name); \
        ok = qfalse; \
    }
    GLE(const GLubyte *, GetString, GLenum name)
    version = (const char *) qglGetString(GL_VERSION);
    if (!version)
        Com_Error(ERR_FATAL, "GL_VERSION is NULL");
    sscanf(version, "%d.%d", &qglMajorVersion, &qglMinorVersion);
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
    if (amix_dpy) {
        glXMakeCurrent(amix_dpy, None, NULL);
        if (amix_ctx)
            glXDestroyContext(amix_dpy, amix_ctx);
        if (amix_win)
            XDestroyWindow(amix_dpy, amix_win);
        XCloseDisplay(amix_dpy);
    }
    amix_dpy = NULL;
    amix_win = 0;
    amix_ctx = NULL;
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

static qboolean set_mode(void)
{
    int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 4, GLX_GREEN_SIZE, 4,
                   GLX_BLUE_SIZE, 4, GLX_DEPTH_SIZE, 16, None };
    XSetWindowAttributes swa;
    XSizeHints hints;
    XVisualInfo *vi;
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
    vi = glXChooseVisual(amix_dpy, scr, attr);
    if (!vi) {
        ri.Printf(PRINT_ALL, "...no RGBA double-buffered visual with a depth buffer\n");
        return qfalse;
    }
    swa.colormap = XCreateColormap(amix_dpy, RootWindow(amix_dpy, scr), vi->visual, AllocNone);
    swa.border_pixel = 0;
    swa.background_pixel = BlackPixel(amix_dpy, scr);
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask | FocusChangeMask | ExposureMask;
    amix_win = XCreateWindow(amix_dpy, RootWindow(amix_dpy, scr), 0, 0,
                             glConfig.vidWidth, glConfig.vidHeight, 0, vi->depth, InputOutput,
                             vi->visual, CWBorderPixel | CWColormap | CWBackPixel | CWEventMask, &swa);
    hints.flags = PSize | PMinSize | PMaxSize;
    hints.width = hints.min_width = hints.max_width = glConfig.vidWidth;
    hints.height = hints.min_height = hints.max_height = glConfig.vidHeight;
    XSetStandardProperties(amix_dpy, amix_win, CLIENT_WINDOW_TITLE, CLIENT_WINDOW_MIN_TITLE,
                           None, NULL, 0, &hints);
    amix_wm_delete = XInternAtom(amix_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(amix_dpy, amix_win, &amix_wm_delete, 1);

    amix_ctx = glXCreateContext(amix_dpy, vi, NULL, True);
    if (!amix_ctx) {
        ri.Printf(PRINT_ALL, "...glXCreateContext failed\n");
        return qfalse;
    }
    XMapWindow(amix_dpy, amix_win);
    do
        XNextEvent(amix_dpy, &ev);
    while (ev.type != MapNotify);
    glXMakeCurrent(amix_dpy, amix_win, amix_ctx);

    glConfig.colorBits = vi->depth;
    glConfig.depthBits = 16;
    glConfig.stencilBits = 0;
    glConfig.isFullscreen = qfalse;
    glConfig.stereoEnabled = qfalse;
    glConfig.displayFrequency = 60;
    XFree(vi);

    if (!get_procs()) {
        ri.Printf(PRINT_ALL, "...the GL lacks functions the renderer needs\n");
        return qfalse;
    }
    return qtrue;
}

static void init_extensions(void)
{
    extern int glAmixSoft3D(void);
    int soft3d = glAmixSoft3D();       /* our libGL: soft3D took the window */

    glConfig.textureCompression = TC_NONE;
    glConfig.textureEnvAddAvailable = qfalse;
    if (extension("GL_EXT_texture_env_add") && r_ext_texture_env_add->integer && !soft3d) {
        glConfig.textureEnvAddAvailable = qtrue;
        ri.Printf(PRINT_ALL, "...using GL_EXT_texture_env_add\n");
    }

    qglMultiTexCoord2fARB = NULL;
    qglActiveTextureARB = NULL;
    qglClientActiveTextureARB = NULL;
    if (soft3d)
        ri.Printf(PRINT_ALL, "...not using GL_ARB_multitexture: soft3D draws one texture\n");
    else if (extension("GL_ARB_multitexture") && r_ext_multitexture->value) {
        GLint units = 0;
        qglMultiTexCoord2fARB = amix_GetProcAddress("glMultiTexCoord2fARB");
        qglActiveTextureARB = amix_GetProcAddress("glActiveTextureARB");
        qglClientActiveTextureARB = amix_GetProcAddress("glClientActiveTextureARB");
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
        qglLockArraysEXT = amix_GetProcAddress("glLockArraysEXT");
        qglUnlockArraysEXT = amix_GetProcAddress("glUnlockArraysEXT");
        if (qglLockArraysEXT && qglUnlockArraysEXT)
            ri.Printf(PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n");
        else
            qglLockArraysEXT = NULL, qglUnlockArraysEXT = NULL;
    }

    textureFilterAnisotropic = qfalse;
    haveClampToEdge = QGL_VERSION_ATLEAST(1, 2) || extension("GL_SGIS_texture_edge_clamp");
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
    if (Q_stricmp(r_drawBuffer->string, "GL_FRONT") != 0)
        glXSwapBuffers(amix_dpy, amix_win);
}
