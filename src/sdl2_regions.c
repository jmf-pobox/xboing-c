/*
 * sdl2_regions.c — Logical render regions.
 *
 * See include/sdl2_regions.h for API documentation.
 * See ADR-009 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_regions.h"

/* =========================================================================
 * Region data
 *
 * Coordinates from stage.c:218-371, computed from:
 *   offsetX = MAIN_WIDTH / 2 = 35
 *   offsetY = MAIN_HEIGHT / 2 = 65
 *   scoreWidth = 224
 *   PLAY_WIDTH = 495, PLAY_HEIGHT = 580
 *   MESS_HEIGHT = 30
 * ========================================================================= */

static const SDL_Rect regions[SDL2RGN_COUNT] = {
    /* Play area: offsetX=35, y=60, 495x580, border=2 */
    [SDL2RGN_PLAY] = {35, 60, 495, 580},

    /* Score: offsetX=35, y=10, 224x42 */
    [SDL2RGN_SCORE] = {35, 10, 224, 42},

    /* Level: scoreWidth+offsetX+25=284, y=5, width=495+35-20-224=286, h=52 */
    [SDL2RGN_LEVEL] = {284, 5, 286, 52},

    /* Message: offsetX=35, y=65+580+10=655, width=495/2=247, h=30 */
    [SDL2RGN_MESSAGE] = {35, 655, 247, 30},

    /* Special: 35+247+10=292, y=655, 180x35 */
    [SDL2RGN_SPECIAL] = {292, 655, 180, 35},

    /* Timer: 35+247+10+180+5=477, y=655, width=495/8=61, h=35 */
    [SDL2RGN_TIMER] = {477, 655, 61, 35},

    /* Editor tool palette: 35+495+15=545, y=60, 120x580 */
    [SDL2RGN_EDITOR] = {545, 60, 120, 580},

    /* Editor type display: x=545, y=65+580+5=650, 120x35 */
    [SDL2RGN_EDITOR_TYPE] = {545, 650, 120, 35},

    /* Dialogue: centered in 575x720 → (575/2)-(381/2)=97, (720/2)-(120/2)=300 */
    [SDL2RGN_DIALOGUE] = {97, 300, 381, 120},
};

/* =========================================================================
 * Public API
 * ========================================================================= */

SDL_Rect sdl2_region_get(sdl2_region_id_t id)
{
    if (id >= 0 && id < SDL2RGN_COUNT)
    {
        return regions[id];
    }
    SDL_Rect zero = {0, 0, 0, 0};
    return zero;
}

sdl2_region_id_t sdl2_region_hit_test(int x, int y)
{
    SDL_Point pt = {x, y};
    for (int i = 0; i < SDL2RGN_COUNT; i++)
    {
        if (SDL_PointInRect(&pt, &regions[i]))
        {
            return (sdl2_region_id_t)i;
        }
    }
    return SDL2RGN_COUNT;
}

const char *sdl2_region_name(sdl2_region_id_t id)
{
    switch (id)
    {
        case SDL2RGN_PLAY:
            return "play";
        case SDL2RGN_SCORE:
            return "score";
        case SDL2RGN_LEVEL:
            return "level";
        case SDL2RGN_MESSAGE:
            return "message";
        case SDL2RGN_SPECIAL:
            return "special";
        case SDL2RGN_TIMER:
            return "timer";
        case SDL2RGN_EDITOR:
            return "editor";
        case SDL2RGN_EDITOR_TYPE:
            return "editor_type";
        case SDL2RGN_DIALOGUE:
            return "dialogue";
        case SDL2RGN_COUNT:
            break;
    }
    return "unknown";
}
