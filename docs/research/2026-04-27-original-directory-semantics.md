# Original directory semantics — research findings

**Author**: jck (Justin C. Kibell persona, vision-keeper)
**Requested by**: claude (COO)
**Date**: 2026-04-27
**Context**: PR #93 — informing the read/write path API split decision
**Status**: Complete; findings adopted in [Path API read/write split spec](../specs/2026-04-27-path-api-read-write-split.md)

## Question

How did the canonical 1996 XBoing source manage asset directories — especially the level editor's read/write semantics?

## Original directory model

The 1996 game used a single directory for all data (levels, sounds, high score), set at compile time via `XBOING_DIR` defaulting to `.` (current working directory). Every call site followed a two-step pattern: check a named env var, fall back to the compile-time constant. The editor loaded and saved levels to the same directory with no concept of a separate writable location.

## Levels (read)

- File: `original/Imakefile:35` — `LEVEL_INSTALL_DIR = $(XBOING_DIR)/levels`
- File: `original/Imakefile:130-135` — this define is passed as `-DLEVEL_INSTALL_DIR=\"...\"` to the compiler
- Call sites:
  - `original/editor.c:186-189` — `DoLoadLevel` for the editor's startup level (`editor.data`)
  - `original/editor.c:857-860` — `LoadALevel` for loading a numbered level into the editor
  - The normal game-play level load flows through `ReadNextLevel` with a path constructed the same way (the `XBOING_LEVELS_DIR` env var check seen in `original/init.c:414-417` in `PrintSetup` confirms the env var was recognized throughout)
- Mechanism: `getenv("XBOING_LEVELS_DIR")` first; if `NULL`, use `LEVEL_INSTALL_DIR`. Single directory, no search path.

## Levels (write — editor)

- File: `original/editor.c:887-931` — `SaveALevel`
- File: `original/editor.c:912-915` — path construction for save:

  ```c
  if ((str2 = getenv("XBOING_LEVELS_DIR")) != NULL)
      snprintf(levelPath, sizeof(levelPath), "%s/level%02ld.data", str2, (u_long)num);
  else
      snprintf(levelPath, sizeof(levelPath), "%s/level%02ld.data", LEVEL_INSTALL_DIR, (u_long)num);
  ```

- **Same dir as read?** Yes. The save path construction at `original/editor.c:912-915` is character-for-character identical to the load path construction at `original/editor.c:857-860`. There is no distinction: the editor read from and wrote to the same directory.
- Secondary note: `original/editor.c:591` uses `tempnam("/tmp", "xboing-")` for the play-test temporary file — this is the one place a different directory appears, but it is a transient file deleted at `original/editor.c:644`, not a saved level.

## Sounds

- File: `original/Imakefile:36` — `SOUNDS_DIR = $(XBOING_DIR)/sounds`
- File: `original/audio/LINUXaudio.c:118` — child process reads `getenv("XBOING_SOUND_DIR")`
- File: `original/audio/LINUXaudio.c:138-141` — same two-step: env var first, `SOUNDS_DIR` fallback
- Mechanism: same pattern as levels — single compile-time directory with env-var override. Sounds were read-only at runtime; there was no write path.

## Read-only vs writable separation

There was none. The original design treated the install directory as the One True Dir for everything. The editor wrote back to `LEVEL_INSTALL_DIR` (or `XBOING_LEVELS_DIR`), the same place it read from. The high-score file (`HIGH_SCORE_FILE`, `original/Imakefile:38`) was similarly a single shared path, made world-writable by the install target (`original/Imakefile:208`: `chmod a+rw`). The game assumed it owned the install tree and had write access to it.

## Modernization implication

The proposed read/writable API split deviates from the 1996 design — but the deviation is forced by modern OS constraints, not a gameplay choice. A system install under `/usr/share/xboing/levels` is EROFS for non-root users; the 1996 assumption of owning the install tree does not hold. Routing editor saves to `$XDG_DATA_HOME/xboing/levels` (or similar user-writable location) while reading from the system data dir honors the original intent (editor can load and save levels) while adapting to Linux multi-user conventions. This is modernization in the "replace technology, not behavior" sense and does not require jck gameplay approval — but the API boundary is documented in `docs/DESIGN.md` ADR-037.

## Citations

- `original/editor.c:186-189, 591, 644, 857-860, 887-931`
- `original/Imakefile:27-38, 129-135, 208`
- `original/init.c:408-428`
- `original/audio/LINUXaudio.c:118-141`
