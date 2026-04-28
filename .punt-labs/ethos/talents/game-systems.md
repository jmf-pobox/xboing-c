# Game Systems

Engine-level concerns specific to interactive game code: frame timing,
deterministic simulation, fixed-timestep loops, input handling, and the
discipline that keeps a game's physics independent of its rendering.

## Fixed Timestep

The simulation must update at a constant rate independent of frame
rate. Variable timestep produces non-deterministic physics — tunneling
through walls at low frame rates, integration drift at high.

```c
const double dt = 1.0 / 60.0;
double accumulator = 0.0;
double current = now();
while (running) {
    double frame = now() - current;
    current += frame;
    accumulator += frame;
    while (accumulator >= dt) {
        update_simulation(dt);
        accumulator -= dt;
    }
    render(accumulator / dt);  /* interpolation factor */
}
```

Reference: Glenn Fiedler, *Fix Your Timestep*. Doom's original
varying timestep on slow hardware is a textbook case of why.

## Determinism

A deterministic game can be replayed, tested, and verified. To stay
deterministic:

- **Seed the RNG.** Never call `time(NULL)` for game-state RNG.
- **Bound the simulation.** Fixed iteration counts, no `while(true)`
  collision-resolve loops.
- **Use the same float ops everywhere.** Compiler flags that change
  rounding (`-ffast-math`) break determinism across builds.
- **Order matters.** Iterate collision pairs in a fixed order, not
  hash-table order.

## Event Loop and Input

The platform's event loop drives input. The game's update loop is
separate — events are queued, then drained inside `update_simulation`.
Mixing them produces input lag and dropped events under load.

## Audio Timing

Sound effects on collision should fire within one frame of the
collision event. SDL2_mixer's `Mix_PlayChannel` is synchronous from
the caller's perspective — the audio callback handles the rest.
Latency budget for game sound: <50ms. Music can tolerate more.

## Save/Load and High Scores

Save formats must be backward compatible across player upgrades.

- Versioned header on every binary save
- Migration path from old format on load
- File locking on high-score writes (process safety)
- Round-trip tests: load → modify → save → load → assert equal

## Integration with the Rest of the Stack

| Concern | Where it lives |
|---------|----------------|
| Game logic systems | `src/*_system.c` (ball, paddle, blocks, score, …) — pure C |
| Renderer | `src/sdl2_renderer.c` (+ `src/sdl2_texture.c`, `src/sdl2_font.c`) |
| Audio | `src/sdl2_audio.c` — SDL2_mixer wrapper |
| Input / event loop | `src/sdl2_loop.c`, `src/game_main.c` |
| Persistence | `src/savegame_io.c`, `src/highscore_io.c` — format-versioned |
| Path resolution | `src/paths.c` — XDG-aware, no platform deps |

The `*_system.c` files in `src/` never include SDL or X11 headers —
they're pure C/game-logic layers. The `sdl2_*.c` files are the
platform boundary: they translate game-render-info structs into
SDL2 calls, and SDL2 events into game-input structs.

## Reference

- Glenn Fiedler, "Fix Your Timestep" (gafferongames.com)
- John Carmack, "Functional Programming in C++" (mostly applies to C)
- Doom 3 BFG release notes (modernization without rewrite)
