#ifndef SDL2_TEXTURE_H
#define SDL2_TEXTURE_H

/*
 * sdl2_texture.h — SDL2 texture loading and caching.
 *
 * Loads PNG images from disk into SDL_Texture objects and caches them
 * in a hash map for O(1) lookup by string key (e.g., "balls/ball1").
 *
 * Opaque context pattern: no globals, fully testable.
 * See ADR-005 in docs/DESIGN.md.
 */

#include <SDL2/SDL.h>

/* Maximum length of a texture cache key (e.g., "balls/ball1"). */
#define SDL2T_MAX_KEY_LEN 128

/* Hash map capacity.  ~180 textures at 256 slots gives ~70% load factor. */
#define SDL2T_MAX_TEXTURES 256

/* Status codes returned by sdl2_texture functions. */
typedef enum
{
    SDL2T_OK = 0,
    SDL2T_ERR_NULL_ARG,
    SDL2T_ERR_RENDERER,
    SDL2T_ERR_IMG_INIT,
    SDL2T_ERR_NOT_FOUND,
    SDL2T_ERR_LOAD_FAILED,
    SDL2T_ERR_CACHE_FULL,
    SDL2T_ERR_KEY_TOO_LONG,
    SDL2T_ERR_SCAN_FAILED
} sdl2_texture_status_t;

/* Read-only texture info returned by sdl2_texture_get(). */
typedef struct
{
    SDL_Texture *texture; /* owned by cache -- do NOT destroy */
    int width;
    int height;
} sdl2_texture_info_t;

/*
 * Configuration for sdl2_texture_create().
 * Use sdl2_texture_config_defaults() for sane starting values,
 * then override individual fields as needed.
 */
typedef struct
{
    SDL_Renderer *renderer; /* required -- borrowed, not owned */
    const char *base_dir;   /* default: "assets/images" */
} sdl2_texture_config_t;

/* Opaque texture cache context -- allocated by create, freed by destroy. */
typedef struct sdl2_texture sdl2_texture_t;

/*
 * Return a config struct populated with default values:
 *   renderer = NULL  (caller must set)
 *   base_dir = "assets/images"
 */
sdl2_texture_config_t sdl2_texture_config_defaults(void);

/*
 * Create a texture cache.  Recursively scans base_dir for .png files,
 * loads each into an SDL_Texture, and inserts it into the hash map.
 *
 * Keys are derived from the file path relative to base_dir, with the
 * .png extension stripped (e.g., "balls/ball1").
 *
 * Partial loads succeed -- individual file failures are logged but do
 * not abort.  Only structural failures (NULL renderer, IMG_Init failure,
 * unreadable base_dir) set *status to an error code.
 *
 * Returns NULL on failure.  On success, *status is SDL2T_OK.
 * The caller owns the returned context and must call sdl2_texture_destroy().
 */
sdl2_texture_t *sdl2_texture_create(const sdl2_texture_config_t *config,
                                    sdl2_texture_status_t *status);

/*
 * Destroy the texture cache: destroys all SDL_Texture objects and frees
 * the context.  Calls IMG_Quit() if this context initialized SDL_image.
 * Safe to call with NULL.
 */
void sdl2_texture_destroy(sdl2_texture_t *ctx);

/*
 * Look up a cached texture by key.
 * Returns SDL2T_OK and fills *info on success; SDL2T_ERR_NOT_FOUND otherwise.
 */
sdl2_texture_status_t sdl2_texture_get(const sdl2_texture_t *ctx, const char *key,
                                       sdl2_texture_info_t *info);

/*
 * Load a single PNG file and insert (or replace) it in the cache under
 * the given key.  This is the test seam: tests can load individual files
 * without scanning a directory tree.
 */
sdl2_texture_status_t sdl2_texture_load_file(sdl2_texture_t *ctx, const char *key,
                                             const char *path);

/* Return the number of textures currently cached. */
int sdl2_texture_count(const sdl2_texture_t *ctx);

/* Return a human-readable string for a status code. */
const char *sdl2_texture_status_string(sdl2_texture_status_t status);

#endif /* SDL2_TEXTURE_H */
