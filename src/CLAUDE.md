# Source Code — Agent Instructions

@docs/BUILDING.md

Read the full build guide above before modifying source.

## Architecture

State machine game loop in `game_main.c`: 16 modes driven by
`sdl2_state_update()` with function pointer dispatch. Mode transitions
via `sdl2_state_transition()`.

### Key Modules

| Module | Role |
|--------|------|
| `game_main.c` | Event loop, drives `sdl2_loop_update` |
| `game_init.c` | Context creation, stub_tick/stub_render callbacks |
| `game_input.c` | Input dispatch: `game_input_global` (once/frame), `game_input_update` (per tick) |
| `game_modes.c` | Mode enter/update/exit handlers, pending flags |
| `game_render.c` | Render dispatch per mode |
| `game_render_ui.c` | Screen-specific renderers (intro, demo, keys, etc.) |
| `game_callbacks.c` | System callback wiring, `attract_cycle[]` table |
| `game_rules.c` | Per-frame rule checks, ball death, level completion |

### Compile Defines

`HIGH_SCORE_FILE`, `LEVEL_INSTALL_DIR`, `SOUNDS_DIR`, `AUDIO_FILE`
default to relative paths. Runtime overrides: `XBOING_SCORE_FILE`,
`XBOING_LEVELS_DIR`, `XBOING_SOUND_DIR`.

## Code Conventions

- `.clang-format` at repo root: LLVM base, 4-space indent, 100 columns, Allman braces
- `snake_case` functions/variables, `UPPER_SNAKE_CASE` macros/constants
- Module-prefix public APIs: `ball_system_update()`, `sfx_system_get_enabled()`
- Every `.c` has a `.h`. Include guards: `#ifndef MODULE_NAME_H`
- System includes first (alphabetized), blank line, project includes (alphabetized)
- Reformat only files you're already modifying (except dedicated formatting passes)
