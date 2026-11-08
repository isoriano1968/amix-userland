/* SDL_opengl.h -- for ioquake3 on the Z3660's core 1: Mesa 18.3's own GL
   headers (OSMesa in the image), for renderercommon/qgl.h; no SDL. */
#ifndef C1_SDL_OPENGL_H
#define C1_SDL_OPENGL_H
#include <GL/gl.h>
#include <GL/glext.h>
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
#ifndef GLAPI
#define GLAPI extern
#endif
#endif
