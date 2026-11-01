/* amix-loader.c -- glad's loader by name, through GLX: AMIX has no dlopen */
#include <glad/gl.h>
#include <GL/glx.h>

static GLADapiproc get(const char *name) { return (GLADapiproc)glXGetProcAddressARB((const GLubyte *)name); }

int amix_gladLoaderLoadGL(void) { return gladLoadGL(get); }
