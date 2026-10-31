/*
 * Texture Manager
 */

#include "zgl.h"

static GLTexture *find_texture(GLContext *c,int h)
{
  GLTexture *t;

  t=c->shared_state.texture_hash_table[h % TEXTURE_HASH_TABLE_SIZE];
  while (t!=NULL) {
    if (t->handle == h) return t;
    t=t->next;
  }
  return NULL;
}

static void free_texture(GLContext *c,int h)
{
  GLTexture *t,**ht;
  GLImage *im;
  int i;

  t=find_texture(c,h);
  if (t->prev==NULL) {
    ht=&c->shared_state.texture_hash_table
      [t->handle % TEXTURE_HASH_TABLE_SIZE];
    *ht=t->next;
  } else {
    t->prev->next=t->next;
  }
  if (t->next!=NULL) t->next->prev=t->prev;

  for(i=0;i<MAX_TEXTURE_LEVELS;i++) {
    im=&t->images[i];
    if (im->pixmap != NULL) gl_free(im->pixmap);
  }

  gl_free(t);
}

GLTexture *alloc_texture(GLContext *c,int h)
{
  GLTexture *t,**ht;
  
  t=gl_zalloc(sizeof(GLTexture));

  ht=&c->shared_state.texture_hash_table[h % TEXTURE_HASH_TABLE_SIZE];

  t->next=*ht;
  t->prev=NULL;
  if (t->next != NULL) t->next->prev=t;
  *ht=t;

  t->handle=h;
  
  return t;
}


void glInitTextures(GLContext *c)
{
  /* textures */

  c->texture_2d_enabled=0;
  c->current_texture=find_texture(c,0);
}

void glGenTextures(int n, unsigned int *textures)
{
  GLContext *c=gl_get_context();
  int max,i;
  GLTexture *t;

  max=0;
  for(i=0;i<TEXTURE_HASH_TABLE_SIZE;i++) {
    t=c->shared_state.texture_hash_table[i];
    while (t!=NULL) {
      if (t->handle>max) max=t->handle;
      t=t->next;
    }

  }
  for(i=0;i<n;i++) {
    textures[i]=max+i+1;
  }
}


void glDeleteTextures(int n, const unsigned int *textures)
{
  GLContext *c=gl_get_context();
  int i;
  GLTexture *t;

  for(i=0;i<n;i++) {
    t=find_texture(c,textures[i]);
    if (t!=NULL && t!=0) {
      if (t==c->current_texture) {
	glBindTexture(GL_TEXTURE_2D,0);
      }
      free_texture(c,textures[i]);
    }
  }
}


void glopBindTexture(GLContext *c,GLParam *p)
{
  int target=p[1].i;
  int texture=p[2].i;
  GLTexture *t;

  assert(target == GL_TEXTURE_2D && texture >= 0);

  t=find_texture(c,texture);
  if (t==NULL) {
    t=alloc_texture(c,texture);
  }
  c->current_texture=t;
}

void glopTexImage2D(GLContext *c,GLParam *p)
{
  int target=p[1].i;
  int level=p[2].i;
  int components=p[3].i;
  int width=p[4].i;
  int height=p[5].i;
  int border=p[6].i;
  int format=p[7].i;
  int type=p[8].i;
  void *pixels=p[9].p;
  GLImage *im;
  unsigned char *pixels1;
  int do_free;

  unsigned char *rgba=NULL;
  int bpp;

  if (!(target == GL_TEXTURE_2D && level == 0 &&
        ((components == 3 && format == GL_RGB) ||
         (components == 4 && format == GL_RGBA)) &&
        border == 0 && type == GL_UNSIGNED_BYTE)) {
    gl_fatal_error("glTexImage2D: combinaison of parameters not handled");
  }

  /* RGBA: soft3D gets the alpha (a 256x256 copy, nearest texel, as below);
     the 68k rasterizer has none and gets the RGB */
  bpp=components;
  if (bpp == 4) {
    unsigned char *src=pixels, *rgb, *q;
    int x,y,i;

    rgba=gl_malloc(256*256*4);
    rgb=gl_malloc(width*height*3);
    if (rgba == NULL || rgb == NULL)
      gl_fatal_error("glTexImage2D: out of memory");
    for (y=0,q=rgba; y<256; y++)
      for (x=0; x<256; x++,q+=4) {
        i=((y*height/256)*width + x*width/256)*4;
        q[0]=src[i]; q[1]=src[i+1]; q[2]=src[i+2]; q[3]=src[i+3];
      }
    for (i=0; i<width*height; i++) {
      rgb[i*3]=src[i*4]; rgb[i*3+1]=src[i*4+1]; rgb[i*3+2]=src[i*4+2];
    }
    pixels=rgb;
  }

  do_free=0;
  if (width != 256 || height != 256) {
    pixels1 = gl_malloc(256 * 256 * 3);
    /* no interpolation is done here to respect the original image aliasing ! */
    gl_resizeImageNoInterpolate(pixels1,256,256,pixels,width,height);
    do_free=1;
    width=256;
    height=256;
  } else {
    pixels1=pixels;
  }

  im=&c->current_texture->images[level];
  im->xsize=width;
  im->ysize=height;
  if (im->pixmap!=NULL) gl_free(im->pixmap);
#if TGL_FEATURE_RENDER_BITS == 24 
  im->pixmap=gl_malloc(width*height*3);
  if(im->pixmap) {
      memcpy(im->pixmap,pixels1,width*height*3);
  }
#elif TGL_FEATURE_RENDER_BITS == 32
  im->pixmap=gl_malloc(width*height*4);
  if(im->pixmap) {
      gl_convertRGB_to_8A8R8G8B(im->pixmap,pixels1,width,height);
  }
#elif TGL_FEATURE_RENDER_BITS == 16
  im->pixmap=gl_malloc(width*height*2);
  if(im->pixmap) {
      gl_convertRGB_to_5R6G5B(im->pixmap,pixels1,width,height);
  }
#else
#error TODO
#endif
  /* soft3D keeps its own 256x256 copy (RGB or RGBA), uploaded on first use;
     a change of format makes it a new firmware texture */
  if (im->s3d_rgb != NULL && im->s3d_bpp != bpp) {
      gl_free(im->s3d_rgb);
      im->s3d_rgb=NULL;
  }
  if (im->s3d_rgb == NULL) im->s3d_rgb=gl_malloc(256*256*bpp);
  if (im->s3d_rgb) {
      memcpy(im->s3d_rgb,bpp == 4 ? rgba : pixels1,256*256*bpp);
      im->s3d_bpp=bpp;
      im->s3d_dirty=1;
  }
  if (do_free) gl_free(pixels1);
  if (rgba) {
      gl_free(rgba);
      gl_free(pixels);                  /* the RGB made above */
  }
}


/* TODO: not all tests are done */
void glopTexEnv(GLContext *c,GLParam *p)
{
  int target=p[1].i;
  int pname=p[2].i;
  int param=p[3].i;

  if (target != GL_TEXTURE_ENV) {
  error:
    gl_fatal_error("glTexParameter: unsupported option");
  }

  if (pname != GL_TEXTURE_ENV_MODE) goto error;

  /* the 68k rasterizer always replaces; soft3D uses the mode */
  if (param != GL_MODULATE && param != GL_REPLACE && param != GL_DECAL &&
      param != GL_BLEND) goto error;
  c->tex_env_mode=param;
}

/* TODO: not all tests are done */
void glopTexParameter(GLContext *c,GLParam *p)
{
  int target=p[1].i;
  int pname=p[2].i;
  int param=p[3].i;
  
  if (target != GL_TEXTURE_2D) {
  error:
    gl_fatal_error("glTexParameter: unsupported option");
  }

  switch(pname) {
  case GL_TEXTURE_WRAP_S:
  case GL_TEXTURE_WRAP_T:
    if (param != GL_REPEAT) goto error;
    break;
  }
}

void glopPixelStore(GLContext *c,GLParam *p)
{
  int pname=p[1].i;
  int param=p[2].i;

  if (pname != GL_UNPACK_ALIGNMENT ||
      param != 1) {
    gl_fatal_error("glPixelStore: unsupported option");
  }
}
