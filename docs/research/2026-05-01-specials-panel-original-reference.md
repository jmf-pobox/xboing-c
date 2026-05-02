# Specials Panel Original Reference

**Research for:** bead xboing-dze — specials panel never drawn in SDL2 integration build
**Mission:** m-2026-05-02-002 (worker: jck, evaluator: jmf-pobox)
**Consumed by:** the implement mission that writes `game_render_specials()` in `src/game_render.c`
**Original source read:** `original/special.c`, `original/misc.c`, `original/stage.c`, `original/paddle.c`, `original/init.c`
**Date:** 2026-05-01

> Note: jck's role is read-only (Read/Grep/Glob/WebFetch). The analytical work in this document is jck's; the COO acted as scribe to file the artifact through the runtime. Systemic fix tracked as follow-up: extend jck's role to include Write/Bash for `docs/research/`, `docs/specs/`, `docs/reviews/` paths so the read-only worker can file its own artifacts.

---

## 1. What `DrawSpecials` does (original/special.c:150-223)

The 1996 function renders 8 labels into a sub-window called `specialWindow`. Each label is drawn in yellow when its special is active, white when inactive. It uses `DrawShadowText`, which draws a black copy at `(x+2, y+2)` before the colored copy at `(x, y)` — see `original/misc.c:141-147`.

### Label grid (from original/special.c:152-222)

```text
Column x=5,   y=3:                 "Reverse"  — active if reverseOn == True
Column x=5,   y=3+ascent+5:        "Sticky"   — active if stickyBat == True

Column x=55,  y=3:                 "Save"     — active if saving == True
Column x=55,  y=3+ascent+5:        "FastGun"  — active if fastGun == True

Column x=110, y=3:                 "NoWall"   — active if noWalls == True
Column x=110, y=3+ascent+5:        "Killer"   — active if Killer == True

Column x=155, y=3:                 "x2"       — active if x2Bonus == True
Column x=155, y=3+ascent+5:        "x4"       — active if x4Bonus == True
```

The y-step between rows is `font->ascent + GAP` where `GAP = 5` (`original/special.c:66`).

`reverseOn` is declared in `original/paddle.c:86` — it is NOT owned by `special.c`. This is why the modern `special_system_get_labels()` takes `reverse_on` as an injected parameter rather than reading it internally.

---

## 2. Font

`DrawSpecials` passes `copyFont` — defined in `original/init.c:96`:

```c
#define COPY_FONT "-adobe-helvetica-medium-r-*-*-12-*-*-*-*-*-*-*"
```

The modern equivalent is `SDL2F_FONT_COPY` (12pt regular, `include/sdl2_font.h:29`).

---

## 3. specialWindow geometry (original/stage.c:263-265)

```c
specialWindow =
    XCreateSimpleWindow(display, mainWindow,
        offsetX + PLAY_WIDTH / 2 + 10,  /* x */
        65 + PLAY_HEIGHT + 10,           /* y */
        180, MESS_HEIGHT + 5,            /* w, h */
        0, white, black);
```

With `offsetX = MAIN_WIDTH / 2 = 35`, `PLAY_WIDTH = 495`, `PLAY_HEIGHT = 580`, `MESS_HEIGHT = 30`:

- **Absolute x in main window:** `35 + 247 + 10 = 292`
- **Absolute y in main window:** `65 + 580 + 10 = 655`
- **Width:** 180
- **Height:** 35

These values are already confirmed in `src/game_render.c:53`:

```c
 * specialWindow: x=292, y=655, w=180, h=35
```

The SDL2 renderer uses a single logical surface for the whole main window (no sub-windows). `game_render_specials` must add these absolute coordinates as its origin offset — coordinates inside the legacy sub-window become `(292 + col_x, 655 + row_y)` in the SDL2 surface.

---

## 4. Row Y calculation

The legacy row-1 y = `3 + font->ascent + 5`.

In SDL2: use `sdl2_font_line_height(ctx->font, SDL2F_FONT_COPY)` as the ascent proxy. `sdl2_font.h:128` returns the line height for a font slot. For Liberation Sans 12pt (the `copyFont` equivalent) the line height is approximately 15px — but the implementer must call `sdl2_font_line_height` at runtime, not hardcode.

Row y values:

```c
int lh = sdl2_font_line_height(ctx->font, SDL2F_FONT_COPY);
int row0_y = SPECIAL_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y;  /* 655 + 3 = 658 */
int row1_y = row0_y + lh + SPECIAL_GAP;                /* 658 + lh + 5  */
```

Where `SPECIAL_PANEL_ORIGIN_Y = 655` (defined by the implementer; see Section 9), `SPECIAL_ROW0_Y = 3`, `SPECIAL_GAP = 5` (the latter two from `include/special_system.h:32-33`).

---

## 5. Active color: yellow; inactive color: white

Original colors from `original/init.c:185-186`:

- `yellow` = X11 "yellow" pixel
- `white` = `WhitePixel(display, ...)`

SDL2 equivalents (as already used in other `game_render.c` functions):

- Active (yellow): `SDL_Color yellow = {255, 255, 50, 255}` — matches `game_render.c:709`
- Inactive (white): `SDL_Color white = {255, 255, 255, 255}`

---

## 6. Shadow text mapping

Legacy: `DrawShadowText(display, specialWindow, copyFont, string, x, y, colour)` (`original/misc.c:141-147`)

- Draws black at `(x+2, y+2)`
- Draws `colour` at `(x, y)`

Modern: `sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_COPY, label, abs_x, abs_y, color)`

- `sdl2_font.h:106` — same shadow semantics, offset `SDL2F_SHADOW_OFFSET = 2` (`sdl2_font.h:18`)

---

## 7. Where to get `reverse_on` in the modern port

Legacy: file-static `reverseOn` in `original/paddle.c:86`.

Modern: `paddle_system_get_reverse(ctx->paddle)` (`include/paddle_system.h:177`). Returns nonzero if reverse is active.

---

## 8. Where to get all 8 label states

Use `special_system_get_labels(ctx->special, reverse_on, labels)` from `include/special_system.h:158-159`. Pass an array of `SPECIAL_COUNT` (8) `special_label_info_t` structs. Each entry has:

- `.label` — display string
- `.col_x` — X offset within the panel (5, 55, 110, or 155)
- `.row` — 0 (top) or 1 (bottom)
- `.active` — 1 = yellow, 0 = white

---

## 9. game_render_specials implementation spec

The implementer must:

1. Add `void game_render_specials(const game_ctx_t *ctx);` to `include/game_render.h`.

2. Implement in `src/game_render.c`:

```c
/* specialWindow: x=292, y=655, w=180, h=35 — see docs/research/2026-05-01-specials-panel-original-reference.md */
#define SPECIAL_PANEL_ORIGIN_X 292
#define SPECIAL_PANEL_ORIGIN_Y 655

void game_render_specials(const game_ctx_t *ctx)
{
    int reverse_on = paddle_system_get_reverse(ctx->paddle);
    special_label_info_t labels[SPECIAL_COUNT];
    special_system_get_labels(ctx->special, reverse_on, labels);

    int lh = sdl2_font_line_height(ctx->font, SDL2F_FONT_COPY);
    SDL_Color yellow = {255, 255, 50, 255};
    SDL_Color white  = {255, 255, 255, 255};

    for (int i = 0; i < SPECIAL_COUNT; i++)
    {
        int abs_x = SPECIAL_PANEL_ORIGIN_X + labels[i].col_x;
        int abs_y = SPECIAL_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y
                    + labels[i].row * (lh + SPECIAL_GAP);
        SDL_Color color = labels[i].active ? yellow : white;
        sdl2_font_draw_shadow(ctx->font, SDL2F_FONT_COPY,
                              labels[i].label, abs_x, abs_y, color);
    }
}
```

1. Call `game_render_specials(ctx)` from `game_render_frame()` in the `SDL2ST_GAME` / `SDL2ST_PAUSE` case in `src/game_render.c` (currently lines 866-885), after `game_render_messages` and `game_render_timer`.

   The panel sits in the bottom strip between `messWindow` and `timeWindow` — always visible during gameplay regardless of pause state. This matches the legacy behavior: the `specialWindow` sub-window was always mapped and updated by `DrawSpecials` calls from `handleGameMode` in `original/main.c`.

---

## 10. Required includes in game_render.c

`game_render.c` already includes `paddle_system.h` and `sdl2_font.h`. It needs:

```c
#include "special_system.h"
```

`game_context.h` already forward-declares `special_system_t` and `paddle_system_t`.

---

## 11. What does NOT need to change

- `special_system_get_labels` already encodes the correct column/row mapping from `original/special.c:150-222`.
- `SPECIAL_PANEL_W=180`, `SPECIAL_PANEL_H=35`, column x offsets, `SPECIAL_GAP=5` are all in `include/special_system.h:26-33` — verified against original.
- No new system API is needed. The entire rendering requires only `special_system_get_labels`, `paddle_system_get_reverse`, `sdl2_font_draw_shadow`, and `sdl2_font_line_height` — all already present in the codebase.

---

## 12. Why the panel is missing today

`game_render_frame` in `src/game_render.c:808` handles `SDL2ST_GAME` / `SDL2ST_PAUSE` (lines 866-885) but never calls `game_render_specials`. The function does not exist yet. This is bead xboing-dze.

---

## Source citations

| Fact | Source |
|------|--------|
| Label text, column x values, row y formula | `original/special.c:152-222` |
| `GAP = 5` | `original/special.c:66` |
| `DrawShadowText` draws shadow at `(x+2, y+2)` then text at `(x, y)` | `original/misc.c:141-147` |
| `copyFont` = 12pt Helvetica medium = `SDL2F_FONT_COPY` | `original/init.c:96` |
| `reverseOn` owned by paddle module | `original/paddle.c:86` |
| `specialWindow` geometry formula | `original/stage.c:263-265` |
| Resolved absolute coordinates (292, 655) | `src/game_render.c:53` |
| Modern label API | `include/special_system.h:158-159` |
| Modern reverse query | `include/paddle_system.h:177` |
| Modern shadow-draw API | `include/sdl2_font.h:106` |
| Modern line height API | `include/sdl2_font.h:128` |
| Modern font slot enum | `include/sdl2_font.h:29` |
| Modern shadow offset constant | `include/sdl2_font.h:18` |
