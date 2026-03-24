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

#include "dialogue_system.h"
#include "game_context.h"
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

    /* Start with the presents splash screen */
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

            /* Route text/key events to dialogue when active */
            if (sdl2_state_current(ctx->state) == SDL2ST_DIALOGUE && ctx->dialogue != NULL)
            {
                if (event.type == SDL_TEXTINPUT)
                {
                    for (int ci = 0; event.text.text[ci] != '\0'; ci++)
                        dialogue_system_key_input(ctx->dialogue, DIALOGUE_KEY_CHAR,
                                                  event.text.text[ci]);
                }
                else if (event.type == SDL_KEYDOWN && !event.key.repeat)
                {
                    switch (event.key.keysym.sym)
                    {
                        case SDLK_RETURN:
                            dialogue_system_key_input(ctx->dialogue, DIALOGUE_KEY_RETURN, '\0');
                            break;
                        case SDLK_BACKSPACE:
                            dialogue_system_key_input(ctx->dialogue, DIALOGUE_KEY_BACKSPACE, '\0');
                            break;
                        case SDLK_ESCAPE:
                            dialogue_system_key_input(ctx->dialogue, DIALOGUE_KEY_ESCAPE, '\0');
                            break;
                        default:
                            break;
                    }
                }
            }

            /* Feed every event to the input module */
            sdl2_input_process_event(ctx->input, &event);
        }

        /* Check for quit action — but not in editor mode (Q is used for editor exit) */
        if (sdl2_input_just_pressed(ctx->input, SDL2I_QUIT) &&
            sdl2_state_current(ctx->state) != SDL2ST_EDIT)
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
