/* SDL_opengl.h -- for ioquake3 on AMIX: Mesa 3.1's GL (OpenGL 1.2), plus
   the later types the qgl.h function lists name (only typedefs come of
   them; the GL1 renderer calls GL 1.x and a few 1.2-era extensions). */
#ifndef AMIX_SDL_OPENGL_H
#define AMIX_SDL_OPENGL_H
#include <GL/gl.h>
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
#ifndef GLAPI
#define GLAPI extern
#endif
#ifndef GL_RGB4_S3TC
#define GL_RGB4_S3TC 0x83A1
#endif
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_SRGB_EXT
#define GL_SRGB_EXT 0x8C40
#endif
#ifndef GL_SRGB8_EXT
#define GL_SRGB8_EXT 0x8C41
#endif
#ifndef GL_SRGB_ALPHA_EXT
#define GL_SRGB_ALPHA_EXT 0x8C42
#endif
#ifndef GL_SRGB8_ALPHA8_EXT
#define GL_SRGB8_ALPHA8_EXT 0x8C43
#endif
#ifndef GL_SLUMINANCE_ALPHA_EXT
#define GL_SLUMINANCE_ALPHA_EXT 0x8C44
#endif
#ifndef GL_SLUMINANCE8_ALPHA8_EXT
#define GL_SLUMINANCE8_ALPHA8_EXT 0x8C45
#endif
#ifndef GL_SLUMINANCE_EXT
#define GL_SLUMINANCE_EXT 0x8C46
#endif
#ifndef GL_SLUMINANCE8_EXT
#define GL_SLUMINANCE8_EXT 0x8C47
#endif
#ifndef GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif
#ifndef GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT
#define GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT 0x8C72
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB 0x84C0
#endif
#ifndef GL_TEXTURE1_ARB
#define GL_TEXTURE1_ARB 0x84C1
#endif
#ifndef GL_MAX_TEXTURE_UNITS_ARB
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MAX_ELEMENTS_VERTICES
#define GL_MAX_ELEMENTS_VERTICES 0x80E8
#endif
#ifndef GL_MAX_ELEMENTS_INDICES
#define GL_MAX_ELEMENTS_INDICES 0x80E9
#endif
typedef char GLchar;
typedef long GLsizeiptr;
typedef long GLintptr;
typedef long long GLint64;
typedef unsigned long long GLuint64;
typedef unsigned short GLhalf;
typedef struct __GLsync *GLsync;
typedef unsigned int GLhandleARB;
typedef char GLcharARB;
#endif
