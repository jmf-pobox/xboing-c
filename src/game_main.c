/*
 * game_main.c -- Main entry point for SDL2-based XBoing.
 *
 * Creates the game context, enters the SDL2 event loop, and drives
 * the fixed-timestep game loop.  The event pump processes SDL events
 * and dispatches them to the input module.  The game loop calls
 * sdl2_state_update() for logic ticks and renders via the stub
 * render callback (replaced by game_render.c in later beads).
 */

#include "game_init.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#include "sdl2_input.h"
#include "sdl2_loop.h"
#include "sdl2_renderer.h"
#include "sdl2_state.h"

int main(int argc, char *argv[])
{
    game_ctx_t *ctx = game_create(argc, argv);
    if (!ctx)
    {
        return EXIT_SUCCESS;
    }

    /* Start in presents mode (splash screen) */
    sdl2_state_transition(ctx->state, SDL2ST_PRESENTS);

    /* --- Event loop ------------------------------------------------------ */

    bool running = true;
    Uint64 last_ticks = SDL_GetTicks64();

    while (running)
    {
        /* Mark start of frame for edge-triggered input */
        sdl2_input_begin_frame(ctx->input);

        /* Process all pending events */
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE)
                        running = false;
                    break;

                default:
                    break;
            }

            /* Feed every event to the input module */
            sdl2_input_process_event(ctx->input, &event);
        }

        /* Check for quit action (Ctrl+Q or similar) */
        if (sdl2_input_just_pressed(ctx->input, SDL2I_QUIT))
            running = false;

        /* Toggle fullscreen on F11 */
        if (sdl2_input_just_pressed(ctx->input, SDL2I_ICONIFY))
            sdl2_renderer_toggle_fullscreen(ctx->renderer);

        /* Calculate elapsed time and drive the game loop */
        Uint64 now = SDL_GetTicks64();
        Uint64 elapsed = now - last_ticks;
        last_ticks = now;

        sdl2_loop_update(ctx->loop, elapsed);
    }

    game_destroy(ctx);
    return EXIT_SUCCESS;
}
