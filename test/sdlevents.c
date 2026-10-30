/* sdlevents -- which events SDL delivers for an idle window (3 s), with the
   renderer presenting a frame now and then, as ffplay does */
#include <stdio.h>
#include "SDL.h"

int main(int argc, char **argv)
{
	static int count[0x10000];
	SDL_Window *w;
	SDL_Renderer *r;
	SDL_Event e;
	Uint32 t0;
	int i, total = 0;

	(void)argc; (void)argv;
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		return 1;
	w = SDL_CreateWindow("sdlevents", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 180, SDL_WINDOW_RESIZABLE);
	r = SDL_CreateRenderer(w, -1, 0);
	t0 = SDL_GetTicks();
	while (SDL_GetTicks() - t0 < 3000) {
		int n = 0;

		while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) {
			count[e.type]++;
			total++;
			if (e.type == SDL_WINDOWEVENT && count[e.type] < 20)
				printf("window event %d\n", e.window.event);
			if (++n > 1000)
				break;
		}
		SDL_Delay(10);
		SDL_PumpEvents();
		SDL_RenderClear(r);
		SDL_RenderPresent(r);
	}
	printf("%d events in 3 s\n", total);
	for (i = 0; i < 0x10000; i++)
		if (count[i])
			printf("  type 0x%x: %d\n", i, count[i]);
	SDL_Quit();
	return 0;
}
