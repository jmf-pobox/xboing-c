# Peer Review: specials-panel-research mission contract

**Reviewer:** sjl (Sam J. Lantinga, av-platform-expert)
**Spec:** `.tmp/missions/specials-panel-research.yaml`
**Date:** 2026-05-01
**Verdict:** accept-with-changes

---

## Summary

The contract is structurally sound. The core read-set covers the right
file and the right entry point (`DrawSpecials` at `original/special.c:150`).
Four findings require revision before registration: one is blocking, three
are non-blocking but will leave the implement mission short of facts it needs.

---

## Findings

### F-1 — BLOCKING: `original/paddle.c` missing from read-set

**Field:** `inputs.references`

`DrawSpecials()` reads `reverseOn` but that variable is **not** declared in
`original/special.c` or `original/include/special.h`. It is declared in
`original/paddle.c:86` as a file-static int (`int reverseOn, stickyOn;`).

Without reading `original/paddle.c`, jck cannot document the ownership of
`reverseOn` or confirm that the modern `special_system.h` correctly models
it as an injected value (the `reverse_on` parameter in
`special_system_get_labels()` and `special_system_get_state()`). The
implement mission needs to know to call `paddle_system_get_reverse()` to
supply that parameter — the research doc cannot explain this without citing
the paddle source.

**Suggested revision:** Add the following line to `inputs.references`:

```yaml
    - original/paddle.c:86          # reverseOn ownership (injected into DrawSpecials read)
```

---

### F-2 — NON-BLOCKING: Font identity not required to be documented

**Field:** `success_criteria`

The contract requires "fonts" to be captured, but specifies no concrete
output format. The implementer needs one specific fact: which SDL2 font slot
maps to `copyFont`. That mapping is already codified in
`include/sdl2_font.h:29` (`SDL2F_FONT_COPY = 3`) — but jck cannot know that
without a reference to `original/init.c:96` where `COPY_FONT` is defined as
`-adobe-helvetica-medium-r-*-*-12-*-*-*-*-*-*-*`.

Without the `original/init.c` reference, jck will document "copyFont" as
an opaque name rather than anchoring it to a concrete XLFD string, which is
the fact that proves `SDL2F_FONT_COPY` (Liberation Sans 12pt Regular) is the
correct slot.

**Suggested revision:** Add to `inputs.references`:

```yaml
    - original/init.c:96            # COPY_FONT XLFD → maps to SDL2F_FONT_COPY
```

And replace the criterion:

```yaml
  - Research doc produced with panel coordinates, label text per special, active/inactive colors, fonts, row/column layout
```

with:

```yaml
  - Research doc produced with panel coordinates, label text per special,
    active/inactive colors (yellow=active, white=inactive, shadow=black),
    font identity (COPY_FONT XLFD and SDL2 equivalent slot), row/column layout
```

---

### F-3 — NON-BLOCKING: `specialWindow` absolute position not in scope of read-set

**Field:** `inputs.references`

The contract tells jck to document "panel coordinates" from `DrawSpecials()`.
`DrawSpecials()` uses *window-local* coordinates (x=5, x=55, x=110, x=155,
y=3). The absolute window-in-mainWindow position (x=292, y=655) comes from
`original/stage.c:263`. The implement mission must blit at the *absolute*
position inside the single SDL2 window, not the window-local position.

The modern `game_render.c` already has the comment at line 53 documenting
`specialWindow: x=292, y=655, w=180, h=35`, so this is derivable, but jck
should cite the original source to close the loop. Without the stage.c
reference, the research doc will likely omit the absolute origin, and the
implementer will have to go back to the original anyway.

**Suggested revision:** Add to `inputs.references`:

```yaml
    - original/stage.c:263          # specialWindow absolute origin (292, 655) in mainWindow
```

And add to `success_criteria`:

```yaml
  - Panel absolute position in the mainWindow documented (x, y from stage.c:CreateAllWindows),
    distinguished from the window-local coordinates used inside DrawSpecials()
```

---

### F-4 — NON-BLOCKING: Output shape does not specify the SDL2 call pattern

**Field:** `success_criteria`

The final criterion says "phrased as actionable input for an implement
mission targeting `src/game_render.c`." That is vague — it does not require
jck to name the specific SDL2 rendering primitives the implement mission
will use (`sdl2_font_draw_shadow` / `SDL2F_FONT_COPY`).

The implement mission will be adding a `game_render_specials()` function to
`src/game_render.c`. Without the research doc specifying which SDL2 font API
to call, the implementer defaults to whatever they already see in the file,
but may choose the wrong font slot or forget the shadow pass.

**Suggested revision:** Replace the final criterion:

```yaml
  - Findings phrased as actionable input for an implement mission targeting src/game_render.c
    (i.e. the implementer can wire the panel without re-reading original/special.c)
```

with:

```yaml
  - Findings phrased as actionable input for an implement mission targeting src/game_render.c.
    Specifically: name the SDL2 call sequence (sdl2_font_draw_shadow with SDL2F_FONT_COPY),
    the state accessor (special_system_get_labels + paddle_system_get_reverse for reverse_on),
    and the absolute window origin to add as offset. The implementer must be able to write
    game_render_specials() without re-reading original/special.c, original/paddle.c, or
    original/stage.c.
```

---

## Read-set completeness check

| Source | Covers | In draft? | Required? |
|--------|--------|-----------|-----------|
| `original/special.c:150` | `DrawSpecials()` full body, `RandomDrawSpecials()`, `GAP` constant | Yes | Yes |
| `original/include/special.h:72` | Legacy API, flag externs | Yes | Yes (confirms flag names) |
| `original/ball.c:834,881,898,960` | Ball→special redraw triggers | Yes | Yes |
| `original/blocks.c:1611,1620,1629` | Block→special redraw triggers | Yes | Yes |
| `original/editor.c:201,620,632` | Editor→special redraw triggers | Yes | Yes |
| `original/paddle.c:86` | `reverseOn` ownership | **No** | **Yes — blocking** |
| `original/init.c:96` | `COPY_FONT` XLFD identity | **No** | Strongly recommended |
| `original/stage.c:263` | `specialWindow` absolute origin | **No** | Strongly recommended |
| `include/special_system.h` | Modern API (already in references) | Yes | Yes |

---

## Success-criteria verifiability check

The existing criteria are objectively checkable (coordinates present, labels
cited by line number, redraw triggers enumerated, attract-mode documented).
F-4 would make the final criterion equally checkable by naming the concrete
SDL2 API. Without that, the leader must make a subjective call on whether
jck's output is "actionable."

---

## Resolution guidance

Address F-1 (blocking) and the three non-blocking findings before
registering the contract. The changes are all additive — no existing
lines need deletion. Total diff: three additional `inputs.references`
entries and two `success_criteria` expansions.
