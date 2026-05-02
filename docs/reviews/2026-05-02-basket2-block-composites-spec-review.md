# Spec Review: Basket 2 Block Composites (basket2-block-composites.yaml)

**Reviewer:** sjl (Sam J. Lantinga — SDL2/rendering)
**Date:** 2026-05-02
**Mission:** m-2026-05-02-022
**Worker (under review):** jdc
**Contract:** `.tmp/missions/basket2-block-composites.yaml`

---

## Verdict

**Accept with changes** — two blocking findings must be resolved before the
worker starts. One adds a line to the success criteria; one corrects an API
misuse that would otherwise produce misaligned overlays. The remaining
findings are non-blocking annotations and scope notes.

---

## Findings

### F1 — BLOCKING — BULLET_BLK offsets are top-anchored, not centered: y+10 overflows the block

**Affected field:** `success_criteria` line 6 (offsets) and implementation
context bullet 3.

**Facts:**

- `assets/images/guns/bullet.png` is 7 x 16 pixels (confirmed via PIL).
- `assets/images/blocks/yellblk.png` is 40 x 20 pixels (confirmed).
- Block height = 20 px. The original offsets place the bullet sprite at
  `y + 10` (`original/blocks.c:1682-1685`). The legacy `DrawTheBullet`
  function draws from that coordinate upward (convention: XBoing draws
  Pixmaps from the top-left corner). With a 16 px tall sprite anchored at
  y+10, the bottom of the sprite lands at y+26 — 6 px below the block
  bottom edge.

**Why this matters in SDL2:** `SDL_RenderCopy` with a `dst` rect places the
texture top-left at the given coordinate. If the worker uses `(x + 6, y + 10)`
as the SDL2 dst.y, the bullet sprite extends 6 px below the block boundary,
overpainting adjacent rows when the block is near the top of the grid.

**What the original did:** `DrawTheBullet` in `original/gun.c` drew the bullet
pixmap with its shape anchored at the passed coordinate. The XPM pixmap for
`bullet` in the original codebase was 7 x 10 px (narrower than the PNG
conversion). The PNG conversion to 7 x 16 added alpha padding. The loaded
PNG's visible content occupies roughly the top 10 px; the bottom 6 rows are
transparent alpha. So `y + 10` is actually vertically centered for the
original 10-tall sprite — and the PNG transparent padding means the SDL2
render visually matches if the worker uses the same coordinate, but the dst
rect is taller than the visible glyph.

**Verdict on offsets:** The (x+6, y+10), (x+15, y+10), (x+24, y+10),
(x+33, y+10) offset values themselves are correct per `original/blocks.c:1682-1685`.
The spec text citing those lines is accurate.

**Action required — revise success criteria to acknowledge bullet sprite
dimensions:**

> Replace success_criteria line 6:
>
> OLD: `game_render_blocks renders 4 SPR_BULLET sprites on top of BULLET_BLK base sprite at offsets (6,10), (15,10), (24,10), (33,10)`
>
> NEW: `game_render_blocks renders 4 SPR_BULLET sprites on top of BULLET_BLK base sprite at dst offsets (6,10), (15,10), (24,10), (33,10) relative to the block top-left, with dst.w=7, dst.h=16 (the actual bullet.png dimensions). Worker must query the texture cache for SPR_BULLET dimensions rather than hard-coding — use sdl2_texture_get to obtain tex.w/tex.h.`

This prevents a future asset swap (if bullet.png is ever re-exported at a
different size) from silently misaligning the composite.

---

### F2 — BLOCKING — `sdl2_font_draw_shadow_centred` centers within a window-relative width, not a block-local bounding box

**Affected field:** implementation context bullet 2 ("use sdl2_font_draw_centred
or equivalent. Position: centered in the block bounding box").

**Facts from `include/sdl2_font.h:113-115` and `src/sdl2_font.c:297`:**

```c
/* sdl2_font_draw_shadow_centred signature */
sdl2_font_status_t sdl2_font_draw_shadow_centred(sdl2_font_t *ctx, sdl2_font_id_t font_id,
                                                 const char *text, int y, SDL_Color color,
                                                 int width);
/* implementation: */
int x = (width / 2) - (text_w / 2);
```

The function computes `x = (width / 2) - (text_w / 2)` and renders at that
absolute `x` coordinate. It has **no `x_origin` parameter** — it assumes the
caller's coordinate system starts at 0. When `game_render_blocks` passes
`width = BLOCK_WIDTH = 40`, the text lands at x = 20 - text_w/2 relative to
the SDL2 window origin (x=0), not the block's pixel position.

The original centers relative to the block's local origin
(`original/blocks.c:1706`): `x1 = x + (20 - w/2)` — note the explicit
additive `x +`. The modern `sdl2_font_draw_shadow_centred` drops the
`x_origin` parameter entirely.

**Options:**

1. Use `sdl2_font_measure` to get text width, then call `sdl2_font_draw` at
   `block_x + (BLOCK_WIDTH/2 - text_w/2)`, `block_y + (BLOCK_HEIGHT/2 - text_h/2)`. This needs no API change and matches original math exactly.
2. Add an `sdl2_font_draw_centred_in_rect` helper that takes an SDL_Rect and centers within it. Higher-level, but adds API surface.

Option 1 matches the original's arithmetic with zero new API and should be
the contract's specified approach.

**Verbatim revision — replace implementation context bullet 2 paragraph:**

> OLD: "render a centered text string using sdl2_font_draw_centred (or equivalent)"
>
> NEW: "render a centered text string using sdl2_font_measure then sdl2_font_draw. Centering math: `x_text = block_x + (BLOCK_WIDTH/2) - (text_w/2)`, `y_text = block_y + (BLOCK_HEIGHT/2) - (text_h/2)`. This matches original/blocks.c:1706 and original/blocks.c:1733. Do NOT use sdl2_font_draw_shadow_centred — its width parameter is a window-relative span with no x-origin, which would misplace the text."

---

### F3 — non-blocking — `sdl2_font_draw_shadow_centred` name mismatch in success criteria

**Affected field:** success criteria lines 4 and 5 reference "using sdl2_font".

Both criteria say "using sdl2_font (verifiable via code inspection of the new
render path)" without naming the specific function. Given F2 above, the spec
text is ambiguous but not wrong. After applying the F2 revision, the criteria
hold as written. No change needed, but jdc should not interpret "using
sdl2_font" to mean calling `sdl2_font_draw_shadow_centred` specifically.

---

### F4 — non-blocking — DROP_BLK sprite key correction in sprite_catalog.h needs a comment update

**Affected field:** `src/sprite_catalog.h` (in write-set).

`sprite_catalog.h:339` currently reads:

```c
case DROP_BLK:
    return SPR_BLOCK_RED; /* Drop uses row-dependent coloring */
```

The comment "row-dependent coloring" is stale — the original always uses
`greenblock` unconditionally (`original/blocks.c:1728`). After the fix to
`SPR_BLOCK_GREEN`, the comment must be updated to prevent future confusion.

**Verbatim revision:**

```c
case DROP_BLK:
    return SPR_BLOCK_GREEN; /* Drop block renders as green (original/blocks.c:1728) */
```

---

### F5 — non-blocking — RANDOM_BLK comment in sprite_catalog.h is stale

**Affected field:** `src/sprite_catalog.h:347`.

Current:

```c
case RANDOM_BLK:
    return SPR_BLOCK_BONUS_1; /* Random uses animated bonus frames */
```

The original (`original/blocks.c:1701`) uses `redblock`, not any bonus frame.
After the fix to `SPR_BLOCK_RED`, update the comment.

**Verbatim revision:**

```c
case RANDOM_BLK:
    return SPR_BLOCK_RED; /* Random block renders as red with "- R -" text overlay (original/blocks.c:1700-1708) */
```

---

### F6 — non-blocking — `hit_points` is already in `block_system_render_info_t`

**Affected field:** success criteria line 7 ("block render-info struct exposes
the data needed for composites ... extend if needed").

`include/block_system.h:91` confirms `hit_points` is already a member of
`block_system_render_info_t`. No struct extension needed. The "extend if
needed" hedge in the contract is resolved: it is not needed. The success
criterion is already satisfied by the existing struct. The worker does not
need to touch `include/block_system.h` or `src/block_system.c` for this
purpose.

---

### F7 — non-blocking — Verifiability: pure-logic seam for text overlay positioning

**Affected field:** contract "Verification" section.

The contract correctly acknowledges composite rendering cannot be unit-tested
directly and accepts visual dogfood verification. However, the centering math
(F2 above) is pure arithmetic that can be extracted and tested. Consider
requiring a helper `block_overlay_center_pos(int block_x, int block_y, int
block_w, int block_h, int text_w, int text_h, int *out_x, int *out_y)` that
test_game_render.c can assert against known inputs. This reduces the visual
dogfood burden to "text looks right" rather than "text is precisely in the
center."

This is **non-blocking** — the contract's current verification approach
(code inspection + dogfood) is acceptable. The seam is worth extracting if
jdc finds the composite logic grows in a second pass.

---

### F8 — non-blocking — Scope sizing: 2 rounds is realistic given no new API or struct work

**Affected field:** `budget.rounds = 2`.

With F2's resolution (use sdl2_font_measure + sdl2_font_draw rather than a
missing centered helper), no new API surface is required and the struct is
already sufficient (F6). Scope is: 3 sprite key changes in sprite_catalog.h
(trivial), composite render logic in game_render.c (moderate — measure +
draw, plus 4-sprite bullet loop), and tests in test_sprite_catalog.c (3
assertions). Two rounds is appropriate.

---

## Summary Table

| ID | Severity | Field | Action |
|----|----------|-------|--------|
| F1 | Blocking | success_criteria line 6 | Revise to require texture-dimension query for bullet dst rect |
| F2 | Blocking | implementation context bullet 2 | Replace sdl2_font_draw_shadow_centred with measure+draw pattern |
| F3 | Non-blocking | success criteria lines 4-5 | No change; note in worker brief |
| F4 | Non-blocking | sprite_catalog.h comment | Update comment on DROP_BLK case |
| F5 | Non-blocking | sprite_catalog.h comment | Update comment on RANDOM_BLK case |
| F6 | Non-blocking | success criteria line 7 | Confirmed satisfied — no struct extension needed |
| F7 | Non-blocking | verification section | Optional seam extraction noted |
| F8 | Non-blocking | budget | 2 rounds confirmed realistic |

---

## Resolution by Leader

To be filled by claude/COO when revising the contract.
