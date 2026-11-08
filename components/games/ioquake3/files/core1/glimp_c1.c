/*
 * glimp_c1.c -- ioquake3's GL on the Z3660's core 1, in place of
 * sdl/sdl_glimp.c: Mesa 18.3's OSMesa (gallium softpipe, the same library
 * the gpu core's zzgl server uses) linked into the image and called
 * directly; no command stream, no 68k in the way.  Each frame goes from
 * OSMesa's buffer into the RTG overlay (c1p_present), over the X window
 * the 68k holds for it.  GL functions by name from OSMesaGetProcAddress.
 *
 * Depth 16, no stencil: what the zzgl build ran with (Q3's blob shadows
 * need none), and half the depth traffic of 24/8.
 */
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>

#ifdef C1_WOLF
#include "renderer/tr_local.h"
#else
#include "renderercommon/tr_common.h"
#endif
#include "c1q3.h"

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

static OSMesaContext ctx;
static uint8_t *fb;

static void *proc(const char *name)
{
    return (void *)OSMesaGetProcAddress(name);
}

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

#define GLE(ret, name, ...) qgl##name = (name##proc *) proc("gl" #name); \
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
    if (ctx)
        OSMesaDestroyContext(ctx);
    ctx = NULL;
    free(fb);
    fb = NULL;
}

void GLimp_Minimize(void)
{
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
    if (!R_GetModeInfo(&glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, r_mode->integer)) {
        glConfig.vidWidth = 640;
        glConfig.vidHeight = 480;
        glConfig.windowAspect = 4.0f / 3.0f;
    }
    if (glConfig.vidWidth > 2048)
        glConfig.vidWidth = 2048;
    ri.Printf(PRINT_ALL, "...setting mode %d: %d %d\n", r_mode->integer, glConfig.vidWidth, glConfig.vidHeight);

    ctx = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
    fb = malloc((size_t)glConfig.vidWidth * glConfig.vidHeight * 4);
    if (!ctx || !fb || !OSMesaMakeCurrent(ctx, fb, GL_UNSIGNED_BYTE, glConfig.vidWidth, glConfig.vidHeight)) {
        ri.Printf(PRINT_ALL, "...OSMesa: no context\n");
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
        qglMultiTexCoord2fARB = proc("glMultiTexCoord2fARB");
        qglActiveTextureARB = proc("glActiveTextureARB");
        qglClientActiveTextureARB = proc("glClientActiveTextureARB");
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
        qglLockArraysEXT = proc("glLockArraysEXT");
        qglUnlockArraysEXT = proc("glUnlockArraysEXT");
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
    r_sdlDriver = ri.Cvar_Get("r_sdlDriver", "osmesa", CVAR_ROM);
    r_allowResize = ri.Cvar_Get("r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH);
    r_centerWindow = ri.Cvar_Get("r_centerWindow", "0", CVAR_ARCHIVE | CVAR_LATCH);
    r_preferOpenGLES = ri.Cvar_Get("r_preferOpenGLES", "0", CVAR_ARCHIVE | CVAR_LATCH);

    ri.Sys_GLimpInit();
    if (!set_mode())
        ri.Error(ERR_FATAL, "GLimp_Init() - could not make the GL context");

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
    qglFinish();
    c1p_present(fb, glConfig.vidWidth, glConfig.vidHeight);
}
