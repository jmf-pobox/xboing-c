# Dialogue Spec Review — 2026-05-26

Spec: docs/specs/2026-05-26-dialogue-renderer-q-key.md

## Blocking Findings + Resolutions

### 1. sdl2_state_previous() returns SDL2ST_DIALOGUE after pop

After pop_dialogue, `previous` is SDL2ST_DIALOGUE, not the pre-dialogue
mode. The abort_pending check must fire before any prev-based logic,
and must not rely on prev to determine origin. Matches wisdom_pending
pattern which also checks the flag first.

**Resolution:** Spec updated. abort_pending check is first in
mode_game_enter, does not inspect prev.

### 2. Q in SDL2ST_DIALOGUE sets quit_pending even if push fails

If Q is pressed during an already-open dialogue, push_dialogue fails
but quit_pending was still set. When the original dialogue closes,
the quit fires unexpectedly.

**Resolution:** Set quit_pending ONLY inside the `if (push == OK)`
block. Also add `mode != SDL2ST_DIALOGUE` guard to Q handler.

### 3. Escape abort must stay inside mode == SDL2ST_GAME

The spec must confirm the Escape→abort path stays scoped to
`mode == SDL2ST_GAME` and is not promoted to global scope.

**Resolution:** Confirmed. Escape abort is inside the existing
`if (mode == SDL2ST_GAME)` block. During SDL2ST_DIALOGUE, Escape
is consumed by dialogue_system_key_input as DIALOGUE_KEY_ESCAPE.

### 4. Dialogue background render missing for attract modes

Q fires from attract modes (INTRO, INSTRUCT, DEMO, etc.) but the
DIALOGUE render case only renders HIGHSCORE and GAME behind the
dialogue. Attract modes would show a black void.

**Resolution:** Add a default branch in the DIALOGUE render case
that calls the attract HUD render path (score, lives, messages,
timer, specials) for any attract mode. Or: render the saved mode
using the existing switch dispatch. Simplest: add all attract modes
to the DIALOGUE background case using the same `is_attract` pattern.

## Non-Blocking Findings

- Question sprite: use original's `(DIALOGUE_WIDTH/2)-16` formula,
  not texture-width centering, for pixel match.
- Message y=by+10, shadow at +2 handled by sdl2_font_draw_shadow.
- Close animation: defer to follow-up, document as ADR.
- Add test: Q-while-dialogue-open does not set quit_pending.
