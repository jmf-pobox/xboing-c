# Testing — Agent Instructions

@docs/TESTING.md

Read the full testing guide above before writing any test.

## Quick Reminders

- Edge-triggered keys → `game_input_global` (once per frame)
- Held keys → `game_input_update` (per tick)
- `SDL_VIDEODRIVER=dummy` for logic tests; `offscreen` only for headless pixel tests (NOT for fidelity goldens)
- `setup_game` fixture sets `use_keys = true`
- Register via `xboing_add_integration_test()` in CMakeLists.txt
- Ball tests: `tick_until_ball_ready` + `launch_ball` helpers
- Visual-fidelity captures: live X11 + ImageMagick pipeline (`make modern-screen`, `make modern-bonus`)
- Deep game state: savegame v2 fixture (`savegame_system_restore`) — see `tools/gen_bonus_fixtures.c`
