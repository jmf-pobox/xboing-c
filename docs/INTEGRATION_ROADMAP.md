# Integration Layer Roadmap

Wiring 35 static libraries (12 game systems, 7 UI sequencers, 4 persistence
modules, 11 SDL2 platform modules, 1 shared arithmetic library) into a
playable SDL2 game binary.

## File Structure

```text
src/
  game_context.h      — master context struct (all module pointers + game state)
  game_main.c         — main(), SDL2 init, event pump, top-level loop
  game_init.c         — create/destroy all modules, wire callback tables
  game_callbacks.c    — callback implementations (sound, score, message, events)
  game_render.c       — rendering dispatch (blocks, balls, paddle, bullets, UI)
  game_render_ui.c    — UI screen rendering (presents, intro, bonus, highscore)
  game_input.c        — input dispatch per game mode
  game_modes.c        — mode enter/exit/update handlers for sdl2_state
  game_rules.c        — game rule logic (level complete, ball death, bonus spawning)
  sprite_catalog.h    — texture key constants and lookup helpers
include/
  game_context.h      — public header for game_ctx_t
```

## Phases

### Phase 1: Bootstrap (COMPLETE)

SDL2 window opens, loads assets, shows a static level with blocks rendered.

- 1.1: Game context struct + sprite catalog
- 1.2: Game initialization and shutdown
- 1.3: Main entry point + event pump
- 1.4: Static level rendering

### Phase 2: Core Gameplay — 6 beads

Paddle moves, ball bounces, blocks break, score updates, levels advance.

- **2.1: Paddle rendering + input** — render paddle, wire LEFT/RIGHT keyboard
  and mouse input. Files: `game_render.c`, `game_input.c`
- **2.2: Ball rendering + launch** — render balls, stub callbacks, space/click
  launches ball. Files: `game_render.c`, `game_callbacks.c`
- **2.3: Block collision + scoring** — wire ball→block collision, score display.
  Files: `game_callbacks.c`, `game_render.c`
- **2.4: Game rules** — level completion, ball death, lives. Files:
  `game_rules.c`, `game_render.c`
- **2.5: Gun/bullet system** — render bullets, K key shoots. Files:
  `game_render.c`, `game_callbacks.c`
- **2.6: Specials + bonus block spawning** — port spawning logic from
  `main.c:handleGameMode()`. Files: `game_rules.c`, `game_render.c`

### Phase 3: Attract Mode Screens — 5 beads

Full startup sequence and post-game screens.

- **3.1: Mode handler framework** — register all 16 mode handlers, implement
  MODE_GAME + MODE_PAUSE. Files: `game_modes.c`
- **3.2: Presents + intro** — splash screen rendering, intro screen. Files:
  `game_render_ui.c`, `game_modes.c`
- **3.3: Demo + preview + keys + instructions** — attract mode cycle. Files:
  `game_render_ui.c`, `game_modes.c`
- **3.4: Bonus sequence** — bonus tally animation after level completion.
  Files: `game_render_ui.c`, `game_modes.c`
- **3.5: High score display + name entry** — score table, name input dialogue.
  Files: `game_render_ui.c`, `game_modes.c`

### Phase 4: SFX, EyeDude, Persistence — 4 beads

- **4.1: SFX rendering** — window shake, screen fade, border glow
- **4.2: EyeDude rendering + collision** — animated character
- **4.3: Save/load game state** — Z/X keys, savegame_io wiring
- **4.4: Config file + startup preferences** — persistent settings

### Phase 5: Level Editor — 2 beads

- **5.1: Editor rendering + input** — block palette, grid editing
- **5.2: Editor play-test mode** — play-test and return to editor

### Phase 6: Polish + Verification — 4 beads

- **6.1: Message bar + timer display**
- **6.2: Block animations + explosion effects**
- **6.3: Verification pass — all 80 levels**
- **6.4: Final cleanup**

## Dependency Graph

```text
Phase 1: 1.1 → 1.2 → 1.3 → 1.4   (COMPLETE)
Phase 2: 1.4 → 2.1 → 2.2 → 2.3 → 2.4
                              ↓
                             2.5 → 2.6
Phase 3: 2.4 → 3.1 → 3.2 → 3.3
                ↓       ↓
               3.4     3.5
Phase 4: 2.6 → 4.1, 4.2  |  2.4 → 4.3  |  1.2 → 4.4
Phase 5: 3.1 → 5.1 → 5.2
Phase 6: ALL → 6.3 → 6.4
```

## Key Risks

1. **Sprite mapping** — 188 XPM→PNG sprites, naming must match texture cache keys
2. **Frame timing** — legacy usleep(ms*400) vs SDL2 fixed timestep
3. **Bonus spawning** — 120-line switch with 27 cases, exact probability distribution
4. **Color cycling** — legacy palette cycling vs SDL_SetTextureColorMod
