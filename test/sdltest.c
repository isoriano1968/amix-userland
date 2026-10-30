/* sdltest -- what ffplay needs from SDL: a window, the renderer with a
   streaming YUV texture, audio by callback, events and timers.  4 seconds. */
#include <stdio.h>
#include <math.h>
#include "SDL.h"

static double phase;
static int rate;

static void tone(void *u, Uint8 *s, int len)
{
	Sint16 *p = (Sint16 *)s;
	int i;

	(void)u;
	for (i = 0; i < len / 2; i++) {
		p[i] = (Sint16)(8000 * sin(phase));
		phase += 2 * M_PI * 440 / rate;
	}
}

int main(int argc, char **argv)
{
	SDL_Window *w;
	SDL_Renderer *r;
	SDL_Texture *t;
	SDL_AudioSpec want, have;
	SDL_AudioDeviceID dev;
	SDL_RendererInfo ri;
	Uint32 t0;
	int frames = 0, quit = 0;
	static Uint8 y[320 * 180], u[160 * 90], v[160 * 90];

	(void)argc; (void)argv;
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
		printf("SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	printf("video %s, audio %s\n", SDL_GetCurrentVideoDriver(), SDL_GetCurrentAudioDriver());
	w = SDL_CreateWindow("sdltest", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 360, SDL_WINDOW_RESIZABLE);
	r = w ? SDL_CreateRenderer(w, -1, 0) : NULL;
	t = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, 320, 180) : NULL;
	if (!t) {
		printf("window/renderer/texture: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GetRendererInfo(r, &ri);
	printf("renderer %s\n", ri.name);
	SDL_zero(want);
	want.freq = 22050;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = tone;
	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	if (dev) {
		rate = have.freq;
		printf("audio %d Hz, %d channel(s), %d samples\n", have.freq, have.channels, have.samples);
		SDL_PauseAudioDevice(dev, 0);
	} else
		printf("audio: %s\n", SDL_GetError());
	t0 = SDL_GetTicks();
	while (!quit && SDL_GetTicks() - t0 < 4000) {
		SDL_Event e;
		int i, j;

		while (SDL_PollEvent(&e))
			if (e.type == SDL_QUIT || e.type == SDL_KEYDOWN)
				quit = 1;
		for (j = 0; j < 180; j++)
			for (i = 0; i < 320; i++)
				y[j * 320 + i] = (Uint8)(i + j + frames * 4);
		memset(u, 90, sizeof u);
		memset(v, 200, sizeof v);
		SDL_UpdateYUVTexture(t, NULL, y, 320, u, 160, v, 160);
		SDL_RenderClear(r);
		SDL_RenderCopy(r, t, NULL, NULL);
		SDL_RenderPresent(r);
		frames++;
	}
	printf("%d frames in %.1f s: %.1f fps\n", frames, (SDL_GetTicks() - t0) / 1000.0,
	    frames * 1000.0 / (SDL_GetTicks() - t0));
	if (dev)
		SDL_CloseAudioDevice(dev);
	SDL_Quit();
	return 0;
}
