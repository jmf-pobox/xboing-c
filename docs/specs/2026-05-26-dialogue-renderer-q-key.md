# Dialogue Renderer Upgrade + Q/Escape Key Handlers

Date: 2026-05-26
Status: APPROVED (peer review findings incorporated)
Review: docs/reviews/2026-05-26-dialogue-spec-review.md

## Context

The dialogue system state machine (`dialogue_system.c`) already handles all 4
validation modes including YES_NO. The renderer (`game_render_dialogue`) is a
minimal dark box that doesn't match the original's visual design. Q and Escape
keys don't open confirmation dialogues — Q quits immediately, Escape only
returns to editor.

## Design

### Renderer (game_render.c — replace game_render_dialogue)

Pixel layout at (bx=92, by=295) in SDL2 logical coordinates:

1. Stone tile background: `SPR_BGRND_MAIN` tiled across 380x120
2. Red border: 4px, `SDL_RenderFillRect` × 4
3. TEXT_ICON sprite: `SPR_TEXT` at (bx+2, by+4), 32x32
4. Green message: shadow text at by+10, x computed via
   `sdl2_font_measure` + `x = bx + (DIALOGUE_WIDTH - text_w) / 2`,
   drawn via `sdl2_font_draw_shadow` (NOT `_centred` which centers
   from x=0)
5. White separator: (bx+10, by+45), width=360, height=2
6. Input text: yellow shadow text at by+70, same centering approach,
   OR `SPR_QUESTION` sprite at (bx + DIALOGUE_WIDTH/2 - 16, by+70)
   when input is empty. Use `(DIALOGUE_WIDTH/2)-16` formula, not
   texture-width centering, to match original pixel-exact.
7. Cursor bar: 2px white after input text end (omit when empty)

### DIALOGUE mode render background

Extend `SDL2ST_DIALOGUE` case in `game_render_frame()` to render the
saved mode's content behind the dialogue overlay. The saved mode can
be ANY mode (GAME, PAUSE, INTRO, INSTRUCT, DEMO, etc. — Q fires from
all modes except EDIT). Dispatch the saved mode through the existing
render switch to draw its full content, then overlay the dialogue.

### Q Key (game_input.c)

Replace immediate SDL_QUIT with:

```c
if (just_pressed(QUIT) && mode != SDL2ST_EDIT && mode != SDL2ST_DIALOGUE)
{
    if (sdl2_state_push_dialogue(state) == SDL2ST_OK)
    {
        dialogue_system_open(..., "Exit XBoing you wimp? [y/n]",
                             DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_YES_NO);
        game_modes_set_quit_pending();
    }
}
```

Critical guards:

- `mode != SDL2ST_DIALOGUE` prevents setting quit_pending during an
  already-open dialogue (finding #2)
- `quit_pending` set ONLY inside `push == OK` block

Result consumed in `mode_dialogue_exit()`:

- If quit_pending && not cancelled && input[0]=='y': SDL_PushEvent(QUIT)
- Otherwise: quit_pending=0, resume

### Escape Key (game_input.c)

Stays inside the existing `if (mode == SDL2ST_GAME)` block (finding #3):

```c
if (just_pressed(ABORT))
{
    if (prev == SDL2ST_EDIT)
        transition(SDL2ST_EDIT);
    else if (sdl2_state_push_dialogue(state) == SDL2ST_OK)
    {
        dialogue_system_open(..., "Abort current game? [y/n]", ...);
        game_modes_set_abort_pending();
    }
}
```

Result consumed in `mode_game_enter()`:

- abort_pending check is FIRST, before any prev-based logic (finding #1)
- After pop_dialogue, `sdl2_state_previous()` returns SDL2ST_DIALOGUE,
  not the pre-dialogue mode — the flag carries the intent, prev is
  not inspected
- If 'y': game_active=false, transition to HIGHSCORE
- If 'n'/Escape/empty: abort_pending=0, return (game resumes)

### Pending Flag Pattern

Follows established `wisdom_pending` pattern in game_modes.c:

- `static int quit_pending, abort_pending` in game_modes.c
- Setter functions exposed via game_modes.h
- Reset in `start_new_game()`
- Consumed in mode exit/enter handlers
- Flag set ONLY when push_dialogue succeeds

### Sound

Wire `dialogue_system_get_sound()` in `mode_dialogue_update` — play
via `sdl2_audio_play` if non-NULL. Sounds: "click" on valid char,
"key" on backspace, "tone" on overflow.

### Close Animation

Deferred to follow-up. The original's 13-frame black grid fade effect
(WindowFadeEffect) has no modern constraint forcing omission — it's a
visual polish item. Will document as ADR when implemented.

### Build Sequence

1. Renderer upgrade (visual only, test with "Words of wisdom" dialogue)
2. Screenshot capture + LLM judge comparison against original
3. Q key handler + quit_pending + tests
4. Escape handler + abort_pending + tests
5. PR

### Tests

Unit tests (dialogue_system pure C, no SDL):

- YES_NO 'y' + Return → not cancelled, input="y"
- YES_NO 'n' + Return → not cancelled, input="n"
- YES_NO Escape → cancelled
- YES_NO empty Return → empty input
- YES_NO rejects 'x'
- YES_NO rejects second char
- State transitions: MAP → TEXT → UNMAP → FINISHED

Integration tests:

- Q opens dialogue (check sdl2_state_current == SDL2ST_DIALOGUE)
- Q during existing dialogue does NOT set quit_pending (finding #8)
- Q dialogue 'n' resumes previous mode
- Q dialogue 'y' pushes SDL_QUIT

## Files Modified

- `src/game_render.c` — rewrite game_render_dialogue, extend DIALOGUE case
- `src/game_input.c` — Q and Escape handlers
- `src/game_modes.c` — quit_pending, abort_pending flags + setters + consumers
- `include/game_modes.h` — setter declarations
- `tests/test_keybindings.c` — YES_NO + Q/Escape + Q-during-dialogue tests
