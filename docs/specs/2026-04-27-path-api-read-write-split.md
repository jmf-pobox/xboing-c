# Spec: Path API read/write split (PR #93 root-cause fix)

**Leader**: claude (COO)
**Worker**: jdc (c-modernization-engineer)
**Reviewer**: gjm (test-engineer) — see [peer review](../reviews/2026-04-27-path-api-read-write-split-review.md)
**Inputs**: [Original directory semantics research](../research/2026-04-27-original-directory-semantics.md)
**Branch**: `fix/installed-asset-paths`
**PR**: #93
**Status**: Reviewed (approve-with-changes), revised, in implementation

## Background

PR #93's stated goal: "installed xboing launches from any cwd." The initial commits patched the three asset subsystems initialized in `src/game_init.c` via `paths_install_data_dir()`. The directory accessors `paths_levels_dir()` and `paths_sounds_dir()` in `src/paths.c` were left returning cwd-relative paths.

Per the [original-source research](../research/2026-04-27-original-directory-semantics.md), 1996 used ONE directory `XBOING_DIR` (default `.`) with subdirs `levels/`, `sounds/`. Editor read and wrote to the same dir — `LEVEL_INSTALL_DIR` or `XBOING_LEVELS_DIR` env override. No read/write separation; install did `chmod a+rw` on data files.

That model is incompatible with modern Linux multi-user installs: system data dirs (`/usr/share/xboing/levels`) are read-only, so editor save fails with EROFS. The modernization deviates from 1996 *only because* the OS forces it.

## Root cause

`paths.h` conflates "where to read system data" with "where to write user data" into a single `paths_levels_dir()` function. The editor uses one function for both load (`editor_system.c:680`) and save (`editor_system.c:722`).

## Fix: explicit read/write API split

### New accessors

```c
/* Read-only resolution for level files.
 * Resolution order:
 *   1. XBOING_LEVELS_DIR env override (cfg->xboing_levels_dir if set)
 *   2. paths_install_data_dir(cfg, "levels")  — XDG_DATA_DIRS lookup
 *   3. cwd-relative "levels"  — dev mode
 * NULL/zero-size args defensively return PATHS_NOT_FOUND. */
paths_status_t paths_levels_dir_readable(const paths_config_t *cfg, char *buf, size_t bufsize);

/* Writable resolution for level files (editor save target).
 * Resolution order:
 *   1. XBOING_LEVELS_DIR env override — applies to both read and write paths;
 *      the user setting this opts into the 1996 single-dir contract and is
 *      responsible for ensuring the dir is writable
 *   2. $XDG_DATA_HOME/xboing/levels  — caller mkdir -p's it before writing
 *   3. cwd-relative "levels"  — dev mode (source tree is writable)
 * The dir is NOT required to exist on disk; caller creates it. */
paths_status_t paths_levels_dir_writable(const paths_config_t *cfg, char *buf, size_t bufsize);

/* Read-only resolution for sounds. No writable counterpart — the editor
 * does not write sounds.
 * Resolution order:
 *   1. XBOING_SOUND_DIR env override
 *   2. paths_install_data_dir(cfg, "sounds")
 *   3. cwd-relative "sounds"
 * NULL/zero-size args defensively return PATHS_NOT_FOUND. */
paths_status_t paths_sounds_dir_readable(const paths_config_t *cfg, char *buf, size_t bufsize);
```

### Removed accessors

`paths_levels_dir()` and `paths_sounds_dir()` are removed from both `include/paths.h` and `src/paths.c`. No backward-compat shim. Fix every call site.

### NULL-arg guard discipline

Every new accessor must guard `cfg == NULL || buf == NULL || bufsize == 0` and return `PATHS_NOT_FOUND` defensively. Match `paths_install_data_dir`'s existing guard at `src/paths.c:377-378`.

## Why this honors the original

- Env overrides (`XBOING_LEVELS_DIR`, `XBOING_SOUND_DIR`) remain highest-precedence — matches `original/editor.c:912-915`.
- Subdir names `levels`/`sounds` match `original/Imakefile:35-36`.
- Deviation: the original wrote to the install dir with `chmod a+rw` for multi-user access (`original/Imakefile:208`); modern Linux forbids that. **Forced deviation, not a behavior choice** — documented in ADR-037.

## Editor changes

Both `src/editor_system.c` and `include/editor_system.h` change.

### Struct field split

```c
typedef struct {
    /* ... existing fields ... */
    char levels_dir_readable[512];   /* for load operations */
    char levels_dir_writable[512];   /* for save operations */
    /* ... */
} editor_system_t;
```

Replaces the single `char levels_dir[512]` field.

### Init signature

```c
editor_system_t *editor_system_create(const editor_system_callbacks_t *callbacks,
                                       void *user_data,
                                       const char *levels_dir_readable,
                                       const char *levels_dir_writable,
                                       int no_sound);
```

The single caller in `src/game_init.c` (around line 562-572) passes both, sourced from `paths_levels_dir_readable()` and `paths_levels_dir_writable()`.

### Call site routing

- `src/editor_system.c:680` (`LoadALevel`-style call) → `ctx->levels_dir_readable`
- `src/editor_system.c:722` (`SaveALevel`-style call) → `ctx->levels_dir_writable`
- `src/editor_system.c:164` (`editor.data` load/save scratch) → `ctx->levels_dir_writable` (it's editor scratch state)

### mkdir-p before first save

Before the first `cb.on_save_level(path, user_data)` call (around line 724), ensure the writable dir exists:

```c
static int mkdir_p(const char *path, mode_t mode);  /* split on '/', mkdir each
                                                       component, treat EEXIST
                                                       as success */
```

On failure: surface via `cb.on_error` (or whatever non-fatal-error callback exists in `editor_system_callbacks_t`); save aborts.

## `-setup` display

`src/game_init.c:146-149` updates to use `_readable` accessors. `-setup` shows where the game *reads* data from — that's the user-visible answer to "where does the game look?". The writable dir is implementation detail.

## Tests

Add a Group 7 section to `tests/test_paths.c`: "Install data dir + read/write directory accessors". Use `mkdtemp(3)` for hermetic per-test temp trees.

### `paths_install_data_dir` (covers Copilot R5+R6)

1. Success: real readable `<tmpdir>/xboing/levels/` → `PATHS_OK`
2. Not found: XDG_DATA_DIRS = `<tmpdir>` (no `xboing/` subdir) → `PATHS_NOT_FOUND`
3. Truncated: buf size 1, valid XDG → `PATHS_TRUNCATED` (must return BEFORE opendir)
4. NULL args (each of `cfg`, `subdir`, `buf`, `bufsize=0`) → `PATHS_NOT_FOUND`
5. (Optional, skip if CI unreliable) Permission-denied: `chmod 000` → `PATHS_NOT_FOUND`

### `paths_levels_dir_readable`

1. Env override returns verbatim
2. Install fallback via mkdtemp tree → returns install path
3. Empty XDG_DATA_DIRS (`""`) → returns `"levels"` verbatim
4. Tiny buf → `PATHS_TRUNCATED`
5. NULL args → `PATHS_NOT_FOUND`

### `paths_levels_dir_writable`

1. Env override returns verbatim (writable contract: user assumes write-perm responsibility)
2. **Env override pointing at a read-only path returns the path anyway** — explicit test that locks this contract decision
3. XDG_DATA_HOME explicit (`"/data/share"`), env unset → returns `/data/share/xboing/levels` (does NOT need to exist on disk)
4. **XDG_DATA_HOME unset** (`xdg_data_home=NULL, home="/home/user"`), env unset → returns `/home/user/.local/share/xboing/levels`
5. Tiny buf → `PATHS_TRUNCATED`
6. NULL args → `PATHS_NOT_FOUND`

### `paths_sounds_dir_readable`

1. Env override (`cfg->xboing_sound_dir`) returns verbatim
2. Install fallback via mkdtemp → install path
3. CWD fallback → `"sounds"` verbatim
4. Truncated → `PATHS_TRUNCATED`
5. NULL args → `PATHS_NOT_FOUND`

### Existing test updates

- Rename / update `test_levels_dir_legacy`, `test_levels_dir_default`, `test_sounds_dir_legacy` to use new accessor names.
- The "default" test currently asserts `"levels"` with default XDG_DATA_DIRS — not hermetic on a host with xboing installed. Switch to explicit empty XDG (`""`).

### Hermetic-test helper

Setup helper creates `<tmpdir>/xboing/<subdir>/`; teardown removes the tree. Tests inject `<tmpdir>` as XDG_DATA_DIRS via `paths_init_explicit`.

## Documentation: ADR-037 update

`docs/DESIGN.md` ADR-037 update covers:

- Read/write API split: three new accessors, two old ones removed.
- Cite `original/editor.c:857-860, 887-931, 912-915` for the 1996 single-dir read/write model.
- Cite `original/Imakefile:35-38, 208` for `LEVEL_INSTALL_DIR`, `SOUNDS_DIR`, and `chmod a+rw`.
- Name the modern Linux constraint forcing deviation: system data dirs read-only on multi-user installs; `chmod a+rw` no longer acceptable.
- Document `XBOING_LEVELS_DIR` overrides BOTH read and write paths (1996 contract preserved); user assumes write-permission responsibility.
- Document no `paths_sounds_dir_writable` (editor does not write sounds).
- Retain opendir-based readability check from earlier commit on this branch.

## Write set (authoritative)

- `src/paths.c`
- `include/paths.h`
- `src/game_init.c`
- `src/editor_system.c`
- `include/editor_system.h`
- `tests/test_paths.c`
- `docs/DESIGN.md`

## Success criteria

1. Three new accessors exist in `include/paths.h` with semantics documented; old accessors removed; no callers of removed names remain.
2. `editor_system_t` has `levels_dir_readable` and `levels_dir_writable` fields; `editor_system_create` signature accepts both; `include/editor_system.h` reflects the changes.
3. Editor's load uses `_readable`; save uses `_writable` and mkdir-p's the dir before first save.
4. `game_init.c` `-setup` display uses `_readable` accessors.
5. `tests/test_paths.c` covers each new accessor + `paths_install_data_dir` per the test plan; existing tests hermetic; all `ctest debug` + `ctest asan` pass.
6. `docs/DESIGN.md` ADR-037 updated with cited original-source references and modernization rationale.
7. `make check` passes.
8. `cd /tmp && /usr/games/xboing` launches a window, no asset errors.
9. Editor save under installed mode succeeds (writes to `$XDG_DATA_HOME/xboing/levels/`), no EROFS.

## Budget

2 review rounds with `reflection_after_each: true`.

## Revision history

- **v1** (2026-04-27): drafted by claude
- **v1 review** (2026-04-27): gjm — approve-with-changes, 2 blocking + 3 minor + 3 test-plan gaps. See [peer review](../reviews/2026-04-27-path-api-read-write-split-review.md).
- **v2** (2026-04-27): claude — incorporated all gjm findings, including struct field split, env-override semantics decision (overrides both paths), mkdir-p discipline, no `_writable` for sounds, null-cfg consistency, three new test cases. Delegated to jdc.
