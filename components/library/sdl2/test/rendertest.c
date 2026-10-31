/* rendertest.c -- the zz9k renderer (soft3D) against the software one on
   AMIX: one picture with every kind of drawing, then frames per second for
   a 320x180 IYUV video texture stretched to the window, as ffplay does.
     rendertest [-sw] [-fs] [-n frames] [-hold seconds]
   SDL_RENDER_DRIVER=software is what -sw sets. */
#include "SDL.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define VW 320
#define VH 180

static Uint8 Y[VW * VH], U[VW * VH / 4], V[VW * VH / 4];

static void frame_yuv(int n)
{
    int x, y;

    for (y = 0; y < VH; y++)
        for (x = 0; x < VW; x++)
            Y[y * VW + x] = (Uint8)(16 + ((x + n * 4) & 255) * 219 / 255);
    for (y = 0; y < VH / 2; y++)
        for (x = 0; x < VW / 2; x++) {
            U[y * VW / 2 + x] = (Uint8)(16 + y * 224 / (VH / 2));
            V[y * VW / 2 + x] = (Uint8)(16 + x * 224 / (VW / 2));
        }
}

int main(int argc, char **argv)
{
    int i, frames = 50, fs = 0, w = 640, h = 360, hold = 3;
    SDL_Window *win;
    SDL_Renderer *r;
    SDL_RendererInfo info;
    SDL_Texture *video, *sprite;
    Uint32 *px, t0, t1;
    SDL_Event e;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-sw"))
            SDL_setenv("SDL_RENDER_DRIVER", "software", 1);
        else if (!strcmp(argv[i], "-fs"))
            fs = 1;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-hold") && i + 1 < argc)
            hold = atoi(argv[++i]);
    }
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("rendertest", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
                           SDL_WINDOW_RESIZABLE | (fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!r)
        r = SDL_CreateRenderer(win, -1, 0);
    if (!r) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GetRendererInfo(r, &info);
    SDL_GetRendererOutputSize(r, &w, &h);
    printf("renderer %s, output %dx%d\n", info.name, w, h);

    video = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, VW, VH);
    sprite = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 64, 64);
    if (!video || !sprite) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return 1;
    }
    px = (Uint32 *)malloc(64 * 64 * 4);
    for (i = 0; i < 64 * 64; i++) {             /* a disc, its alpha fading out */
        int x = i % 64 - 32, y = i / 64 - 32, d = x * x + y * y;
        Uint32 a = d < 1024 ? (Uint32)(255 - d * 255 / 1024) : 0;
        px[i] = a << 24 | 0xff0000 | (Uint32)((i % 64) * 4) << 8 | 0x40;
    }
    SDL_UpdateTexture(sprite, NULL, px, 64 * 4);
    SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_BLEND);

    /* the picture: video top left, a grid of rectangles, lines, points,
       blended sprites, a rotated copy, a Gouraud triangle */
    frame_yuv(0);
    SDL_UpdateYUVTexture(video, NULL, Y, VW, U, VW / 2, V, VW / 2);
    SDL_SetRenderDrawColor(r, 0, 0, 64, 255);
    SDL_RenderClear(r);
    {
        SDL_Rect d = { 10, 10, VW, VH };
        SDL_RenderCopy(r, video, NULL, &d);
    }
    for (i = 0; i < 8; i++) {
        SDL_Rect q = { 340 + (i % 4) * 70, 10 + (i / 4) * 50, 60, 40 };
        SDL_SetRenderDrawColor(r, (Uint8)(i & 1 ? 255 : 0), (Uint8)(i & 2 ? 255 : 0), (Uint8)(i & 4 ? 255 : 0), 255);
        SDL_RenderFillRect(r, &q);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 128);
    {
        SDL_Rect q = { 360, 30, 240, 60 };
        SDL_RenderFillRect(r, &q);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
    for (i = 0; i < 10; i++)
        SDL_RenderDrawLine(r, 340, 120 + i * 6, 620, 200 - i * 6);
    SDL_RenderDrawLine(r, 340, 210, 620, 210);
    SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
    for (i = 0; i < 280; i += 4)
        SDL_RenderDrawPoint(r, 340 + i, 220);
    for (i = 0; i < 5; i++) {
        SDL_Rect d = { 20 + i * 40, 200, 64, 64 };
        SDL_RenderCopy(r, sprite, NULL, &d);
    }
    {
        SDL_Rect d = { 260, 220, 96, 96 };
        SDL_RenderCopyEx(r, sprite, NULL, &d, 30.0, NULL, SDL_FLIP_NONE);
    }
    {
        SDL_Vertex v[3] = {
            { { 400, 340 }, { 255, 0, 0, 255 }, { 0, 0 } },
            { { 620, 250 }, { 0, 255, 0, 255 }, { 0, 0 } },
            { { 620, 350 }, { 0, 0, 255, 255 }, { 0, 0 } },
        };
        SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
    }
    {
        SDL_Rect off = { w - 40, h - 40, 100, 100 };    /* clipped by the window */
        SDL_SetRenderDrawColor(r, 255, 0, 255, 255);
        SDL_RenderFillRect(r, &off);
    }
    SDL_RenderPresent(r);
    for (i = 0; i < hold * 10; i++) {
        while (SDL_PollEvent(&e))
            ;
        SDL_Delay(100);
    }

    /* speed: the video stretched to the whole output, new texels each frame */
    t0 = SDL_GetTicks();
    for (i = 0; i < frames; i++) {
        frame_yuv(i);
        t1 = SDL_GetTicks();
        SDL_UpdateYUVTexture(video, NULL, Y, VW, U, VW / 2, V, VW / 2);
        SDL_RenderCopy(r, video, NULL, NULL);
        SDL_RenderPresent(r);
        while (SDL_PollEvent(&e))
            ;
    }
    t1 = SDL_GetTicks();
    printf("%d frames %dx%d in %u ms: %.1f fps (incl. making the test frames)\n",
           frames, w, h, (unsigned)(t1 - t0), frames * 1000.0 / (t1 - t0 ? t1 - t0 : 1));
    SDL_DestroyTexture(video);
    SDL_DestroyTexture(sprite);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
