/*
 * game_main.c -- Main entry point for SDL2-based XBoing.
 *
 * Placeholder — creates and immediately destroys the game context.
 * Bead 1.3 will add the full event pump and game loop.
 */

#include "game_init.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    game_ctx_t *ctx = game_create(argc, argv);
    if (!ctx)
    {
        /* game_create returns NULL for -help/-version (exit 0) or errors (exit 1).
         * For now, just exit cleanly. */
        return EXIT_SUCCESS;
    }

    printf("xboing_sdl2: context created successfully, shutting down.\n");

    game_destroy(ctx);
    return EXIT_SUCCESS;
}
