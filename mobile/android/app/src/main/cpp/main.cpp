#include <SDL.h>
#include <android/log.h>

int SDL_main(int argc, char* args[]) {
	__android_log_print(ANDROID_LOG_ERROR, "TeenyCode", "Starting main");

	SDL_Window* window;
	SDL_Renderer* renderer;

	/* Initialize SDL. */
	if(SDL_Init(SDL_INIT_VIDEO) < 0) {
		return 2;
	}

	/* Create the window where we will draw. */
	window
		= SDL_CreateWindow("TeenyCodes", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, 0);

	renderer = SDL_CreateRenderer(window, -1, 0);
	/* Select the color for drawing. It is set to red here. */
	SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);

	bool done = false;
	SDL_Event event;
	while(!done) {
		while(SDL_PollEvent(&event)) {
			if(event.type == SDL_QUIT)
				done = 1;
		}

		SDL_RenderClear(renderer);
		SDL_RenderPresent(renderer);

		SDL_Delay(16);
	}

	SDL_Quit();

	return 0;
}