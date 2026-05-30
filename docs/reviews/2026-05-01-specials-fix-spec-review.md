# Peer Review: specials-panel-fix mission contract

**Reviewer:** gjm (Glenford J. Myers, test-expert)
**Spec:** `.tmp/missions/specials-panel-fix.yaml`
**Date:** 2026-05-01
**Verdict:** accept-with-changes

---

## Summary

The contract commissions sjl to add `game_render_specials()` to
`src/game_render.c` and wire it into `game_render_frame()`. The research
reference (section 9 of `docs/research/2026-05-01-specials-panel-original-reference.md`)
is thorough and provides a precise implementation sketch. The write-set is
correct and minimal.

Four findings require revision. Two are blocking: the contract lacks a
machine-verifiable coordinate test (the only verification criterion that
requires no screen), and it does not require sjl to handle the NULL-pointer
path for `ctx->special` and `ctx->paddle`. Two are non-blocking but address
regression resistance and a missing include in the write-set.

---

## Findings

### F-1 — BLOCKING: No machine-verifiable success criterion

**Field:** `success_criteria`

Every criterion listed is either "function defined" (source inspection only),
"make check passes" (build + sanitizer pass, but does not cover rendering
logic), or "panel visible when running the build" (requires a human to look
at the screen). None of them can be evaluated by ctest without a display.

The coordinate arithmetic in `game_render_specials()` is testable without
a display. `special_system_get_labels()` returns `col_x` and `row` fields.
The absolute pixel positions are deterministic:

```text
abs_x = SPECIAL_PANEL_ORIGIN_X + labels[i].col_x
abs_y = SPECIAL_PANEL_ORIGIN_Y + SPECIAL_ROW0_Y + labels[i].row * (lh + SPECIAL_GAP)
```

`SPECIAL_PANEL_ORIGIN_X=292`, `SPECIAL_PANEL_ORIGIN_Y=655`,
`SPECIAL_ROW0_Y=3`, `SPECIAL_GAP=5` are all compile-time constants defined in
`include/special_system.h:26-33`. The only runtime value is `lh =
sdl2_font_line_height(ctx->font, SDL2F_FONT_COPY)`, and `sdl2_font_line_height`
returns 0 for a NULL `ctx` argument (documented in `include/sdl2_font.h:128`).

A characterization test in `tests/test_special_system.c` (already exists,
group 5) can verify the label coordinate outputs without any SDL2 context.
A pure-logic helper, or a new test group in that file, can assert exact
pixel positions for every label given known column and row values.

Additionally, a `tests/test_game_render_specials.c` unit test can stub
`sdl2_font_draw_shadow` via a CMocka mock to capture `(abs_x, abs_y, color)`
for each call and assert against expected values — all without a real
renderer.

**Suggested revision — replace the "Panel visible" criterion with:**

```yaml
  - tests/test_game_render_specials.c added: CMocka test verifying that
    game_render_specials_coords() (pure helper extracted from the render
    function, see F-3) returns the correct abs_x, abs_y for each of the 8
    labels given known lh, verified against the constants in
    include/special_system.h without any SDL2 context
  - Active label 0 ("Reverse") maps to abs_x=297, abs_y=658 (
    SPECIAL_PANEL_ORIGIN_X+SPECIAL_COL0_X=292+5,
    SPECIAL_PANEL_ORIGIN_Y+SPECIAL_ROW0_Y=655+3) — verified by test
  - Inactive labels produce white SDL_Color {255,255,255,255}; active labels
    produce yellow {255,255,50,255} — verified by test
```

---

### F-2 — BLOCKING: No NULL-guard requirement for ctx->special and ctx->paddle

**Field:** `success_criteria` and `context`

The implementation sketch in research section 9 calls:

```c
int reverse_on = paddle_system_get_reverse(ctx->paddle);
special_label_info_t labels[SPECIAL_COUNT];
special_system_get_labels(ctx->special, reverse_on, labels);
```

`ctx->paddle` and `ctx->special` are pointer fields in `game_ctx_t`
(`include/game_context.h:77,81`). The contract does not require sjl to guard
against these being NULL. Other render functions in `game_render.c` do check
for NULL system pointers before dereferencing — sjl must follow the same
pattern.

`paddle_system_get_reverse(NULL)` and `special_system_get_labels(NULL, ...)`
are not documented to be NULL-safe in their headers. If either system is not
yet initialized (e.g., during early startup or after teardown) and
`game_render_frame()` fires anyway, the process crashes under ASan.

`test_special_system.c:test_get_state_null_ctx` (line 361) already exists
and shows that `special_system_get_state(NULL, ...)` is handled — but
`special_system_get_labels(NULL, ...)` is not tested for the NULL case.
This gap must be closed.

**Suggested revision — add to `success_criteria`:**

```yaml
  - game_render_specials() guards against ctx->special == NULL and
    ctx->paddle == NULL: returns immediately without dereferencing either
  - tests/test_game_render_specials.c includes a test asserting that
    calling game_render_specials() with a ctx whose special and paddle
    fields are NULL does not crash (sanitizer build must pass)
```

**Suggested revision — add to `context`:**

```yaml
  NULL-guard pattern: if (ctx->special == NULL || ctx->paddle == NULL) return;
  as the first lines of game_render_specials(). Match the pattern used by
  other render functions in src/game_render.c.
```

---

### F-3 — NON-BLOCKING: No regression test requirement in write-set

**Field:** `write_set` and `success_criteria`

The write-set does not include `tests/test_game_render_specials.c`. Without
a test file in the write-set, there is no structural guarantee sjl writes one.
The "make check passes" criterion verifies the build but not rendering
correctness. Six months from now, a refactor that accidentally swaps two
column constants (e.g., `SPECIAL_COL1_X` and `SPECIAL_COL2_X`) will pass
`make check` and pass visual inspection unless someone specifically checks
the panel layout under all 8 states.

The coordinate math is pure enough to test without a display: extract a
helper function `game_render_specials_coords(int lh, const special_label_info_t *labels,
int i, int *abs_x, int *abs_y)` that computes coordinates for label `i` given
a line height. This function has no SDL2 dependency and can be tested in
isolation. The test becomes the regression lock.

**Suggested revision — add to `write_set`:**

```yaml
  - tests/test_game_render_specials.c
```

**Suggested revision — add to `success_criteria`:**

```yaml
  - tests/test_game_render_specials.c added to tests/CMakeLists.txt and
    registered as a ctest target
```

---

### F-4 — NON-BLOCKING: Missing `#include "special_system.h"` direction in context

**Field:** `context`

Research section 10 explicitly states that `game_render.c` will need:

```c
#include "special_system.h"
```

The contract's context field references section 9 of the research doc as the
spec. Section 9 does not include this include directive — it is in section 10.
There is a risk sjl reads only section 9 and omits the include, producing a
compile error that `ctest debug` will catch but only after time is wasted
debugging. The contract should reference section 10 explicitly or restate the
include requirement.

**Suggested revision — replace the last sentence of `context`:**

```yaml
  context: |
    Wire the specials panel into the SDL2 integration build per the
    canonical reference at docs/research/2026-05-01-specials-panel-original-reference.md.
    The implementation sketch in section 9 of that document is the spec —
    add game_render_specials() to src/game_render.c and call it from
    game_render_frame() in the SDL2ST_GAME / SDL2ST_PAUSE branch after
    game_render_messages and game_render_timer. No new module APIs;
    every needed call site (special_system_get_labels,
    paddle_system_get_reverse, sdl2_font_draw_shadow, sdl2_font_line_height)
    already exists. Read sections 9 AND 10 of the research doc; section 10
    specifies the required #include "special_system.h" that must be added
    to game_render.c. Do NOT re-read original/.
```

---

## Testability assessment

The contract as written has one machine-verifiable criterion: `make check
passes`. That is a build and sanitizer gate, not a behavioral gate. The
coordinate arithmetic and the active/inactive color selection in
`game_render_specials()` are verifiable by unit test without a display. Both
the coordinate math and the color-selection branch are worth locking with
tests before sjl ships — if the panel position is wrong by 10px, no CI check
fires. A characterization test costs one test file and makes the fix
self-defending.

`test_special_system.c` group 5 already tests `special_system_get_labels()`
output thoroughly, including column x-values and row numbers. The gap is
on the `game_render` side: nobody tests that those label coordinates are
correctly translated to absolute pixel positions with the right panel origin.

## Regression resistance assessment

Without F-3 addressed, the regression mechanism for this fix is "a human
notices the panel is missing or misaligned when visually inspecting the game."
That is not a regression mechanism — it is absence of one. Constants like
`SPECIAL_PANEL_ORIGIN_X=292` will survive indefinitely once defined, but the
coordinate-assembly loop in `game_render_specials()` has three
interacting values (`col_x`, `row`, `lh`) whose product is untested. A future
line-height change in `sdl2_font` (e.g., switching from Liberation Sans to
a different metrics font) would silently shift all row-1 labels down by
the difference in line height. A test that asserts `row1_y == row0_y + lh + 5`
catches that immediately.

## Edge cases not covered by the contract

1. `ctx->special == NULL` or `ctx->paddle == NULL` — addressed in F-2.
2. `sdl2_font_line_height()` returns 0 (font not loaded, or invalid slot).
   When `lh = 0`, `row1_y = row0_y + 0 + 5 = row0_y + 5`, which collapses
   both rows to nearly the same y. The contract does not require a guard
   or a minimum line-height assertion. This is a non-blocking edge case
   but worth noting: if `lh == 0`, the panel will render visually garbled
   rather than crashing, so it is lower severity than the NULL-pointer cases.
3. `paddle_system_get_reverse()` returns a value other than 0 or 1 (any
   nonzero value means reverse is active). The implementation sketch uses
   `reverse_on` as a boolean argument to `special_system_get_labels()`, which
   treats any nonzero value as true. No issue here — the API contract in
   `include/paddle_system.h:177` says "return nonzero if active," and
   `special_system_get_labels()` treats `reverse_on` as a boolean. This is
   correct and does not need a guard.

---

## Resolution checklist for leader

| Finding | Severity | Action |
|---------|----------|--------|
| F-1: No machine-verifiable criterion | Blocking | Add coordinate test requirement to success_criteria; add tests/ to write_set |
| F-2: No NULL-guard requirement | Blocking | Add NULL-guard criterion + context note |
| F-3: No test file in write_set | Non-blocking | Add tests/test_game_render_specials.c to write_set |
| F-4: Missing include direction | Non-blocking | Extend context to reference section 10 |
