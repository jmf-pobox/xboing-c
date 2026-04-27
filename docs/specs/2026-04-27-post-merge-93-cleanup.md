# Spec: Post-merge cleanup of PR #93 + dogfood wrapper

**Leader**: claude (COO)
**Worker**: jdc (c-modernization-engineer)
**Reviewer**: gjm (test-engineer) — see [peer review](../reviews/2026-04-27-post-merge-93-cleanup-review.md)
**Inputs**:

- [PR #93 spec](2026-04-27-path-api-read-write-split.md) — the merged change
- [PR #93 spec peer review](../reviews/2026-04-27-path-api-read-write-split-review.md)
- Post-merge code review (pr-review-toolkit) — 4 findings, 1 warning + 3 notes, no critical

**Branch**: `chore/post-merge-93-cleanup`
**PR**: TBD

## Background

PR #93 merged at SHA `84bdeec` with 4 post-merge code-reviewer findings (1 warning, 3 notes — none critical) and one process gap: the COO never installed the `.deb` and verified launch from `.tmp/` (Phase 5 dogfood requirement in CLAUDE.md). This branch addresses both.

## Findings (from post-merge code review of #93)

### Warning

**W1 — `include/paths.h:130`** — `paths_levels_dir_writable` docstring lists a CWD fallback ("step 3: cwd-relative 'levels' — dev mode") that the implementation never reaches. `src/paths.c:381-396` only implements steps 1 (env override) and 2 (`$XDG_DATA_HOME/xboing/levels`). Step 2 always succeeds because `paths_init_explicit` defaults `cfg->xdg_data_home` to `$HOME/.local/share`. The third branch is unreachable.

Fix: **drop the unreachable doc line** (option B from the reviewer). The behavior is correct — editor saves go to `$XDG_DATA_HOME/xboing/levels` even in dev mode — only the documentation is wrong. `tests/test_paths.c::test_levels_dir_writable_home_fallback` already locks the real behavior.

### Notes

**N1 — `include/xboing_paths.h:10-22`** — Duplicated explanation of `XBOING_DATA_DIR` override (two paragraphs cover the same `-D` mechanism). Reads as an unmerged drafting artifact.

Fix: merge into one block — keep the quoting requirement and the Debian Policy default in a single paragraph.

**N2 — `src/paths.c:393-396`** — Dead-style `if/return st; return PATHS_OK;` tail on the writable path:

```c
paths_status_t st = build_path(buf, bufsize, cfg->xdg_data_home, "xboing", "levels", NULL);
if (st != PATHS_OK)
    return st;
return PATHS_OK;
```

Fix: collapse to `return build_path(buf, bufsize, cfg->xdg_data_home, "xboing", "levels", NULL);` — matches the surrounding style (e.g. `paths_user_data_dir` at line 408).

**N3 — `src/game_init.c:142-148`** — `xboing -setup` shows `Levels dir` (readable resolution only) and `Sounds dir`. The editor save target — now a distinct location under `$XDG_DATA_HOME/xboing/levels` — is not visible. A user asking "where will my edited level land?" can't discover it from `-setup`.

Fix: add a fourth line driven by `paths_levels_dir_writable`, gated on the same `PATHS_OK` check pattern. Output format:

```text
  Levels dir          = /usr/share/xboing/levels
  Levels dir (editor) = /home/user/.local/share/xboing/levels
  Sounds dir          = /usr/share/xboing/sounds
```

## Dogfood wrapper (process gap)

CLAUDE.md Phase 5 requires installing the `.deb` and walking the launch journey from a non-source-tree cwd. Doing this manually requires reaching outside the repo (`../*.deb`, `cd /tmp`), which the user's permission model rejects. Add a `make dogfood` target that handles the flow inside the repo's file I/O constraints.

### `make dogfood` target

```makefile
dogfood: deb
	@mkdir -p .tmp
	@rm -f .tmp/xboing_*.deb .tmp/dogfood.deb
	@ver="$$(dpkg-parsechangelog -S Version)"; \
	arch="$$(dpkg --print-architecture)"; \
	expected="../xboing_$${ver}_$${arch}.deb"; \
	if [ ! -f "$$expected" ]; then \
	    echo "FAIL: expected package $$expected not found (did make deb succeed?)"; \
	    exit 1; \
	fi; \
	cp "$$expected" .tmp/dogfood.deb; \
	sudo dpkg -i .tmp/dogfood.deb
	@( cd .tmp && exec /usr/games/xboing ) & echo $$! > .tmp/dogfood.pid
	@sleep 3
	@if [ -n "$$DISPLAY" ] || [ -n "$$WAYLAND_DISPLAY" ]; then \
	    if command -v xwininfo >/dev/null 2>&1; then \
	        xwininfo -name XBoing > .tmp/dogfood-window.txt 2>&1 || { \
	            echo "FAIL: xboing window not detected"; \
	            kill "$$(cat .tmp/dogfood.pid)" 2>/dev/null || true; \
	            rm -f .tmp/dogfood.pid; \
	            exit 1; \
	        }; \
	        echo "PASS: xboing launched from .tmp/, window detected"; \
	    else \
	        if kill -0 "$$(cat .tmp/dogfood.pid)" 2>/dev/null; then \
	            echo "INFO: display detected but xwininfo unavailable; window check skipped"; \
	            echo "PASS: xboing started from .tmp/ without immediate crash"; \
	        else \
	            echo "FAIL: xboing exited before window-detection grace period"; \
	            rm -f .tmp/dogfood.pid; \
	            exit 1; \
	        fi; \
	    fi; \
	else \
	    if kill -0 "$$(cat .tmp/dogfood.pid)" 2>/dev/null; then \
	        echo "INFO: no display detected (\$$DISPLAY/\$$WAYLAND_DISPLAY unset); window check skipped"; \
	        echo "PASS: xboing started from .tmp/ without immediate crash"; \
	    else \
	        echo "FAIL: xboing exited before window-detection grace period"; \
	        rm -f .tmp/dogfood.pid; \
	        exit 1; \
	    fi; \
	fi
	@kill "$$(cat .tmp/dogfood.pid)" 2>/dev/null || true
	@rm -f .tmp/dogfood.pid
```

Notes for jdc on the wrapper:

- Use POSIX-sh recipes (the user runs Debian/Ubuntu, default `/bin/sh` is dash). Avoid bashisms (`[[`, `==`, arrays, etc.). Test with `dash -n Makefile-recipe` if uncertain.
- The launched `xboing` PID is captured via `echo $$! > .tmp/dogfood.pid` and used for the targeted `kill` later. **Do not use `pkill -x xboing`** — that would race against any other `xboing` instance the developer has running and silently kill the wrong process.
- The window-detection probe runs only when `$DISPLAY` or `$WAYLAND_DISPLAY` is set. Headless environments (VMs, remote SSH without X forwarding, CI runners) skip the probe and instead verify the process is still alive after the 3-second grace period — that catches install-without-crash without falsely failing on missing-display environments.
- The `xwininfo -name XBoing` output is written to `.tmp/dogfood-window.txt` (inside the repo, gitignored) so the artifact is inspectable. The probe is itself guarded by `command -v xwininfo` so the recipe degrades to a process-liveness check on systems where `xwininfo` is not installed.
- `sudo dpkg -i` will prompt for password — that's expected user interaction, not a defect. Document it in the help text.
- Add a `make help` entry for `dogfood`: `dogfood         Install .deb, launch xboing from .tmp/, verify window opens (requires sudo; skips window check if headless).`
- `dogfood` depends on `deb` (which is in the existing Makefile per `make help`); the dependency ensures the `.deb` exists before copy.
- All shell continuation uses `\` at end of line, with the entire conditional block as a single recipe line. The `if/elif/else/fi` block is one shell command from Make's perspective.

### Why this fits inside the permission model

- `dpkg-buildpackage` writes the `.deb` to `..` per its hardcoded convention — that's fine because `make deb` is the wrapper, and the user granted `make *`.
- The `.deb` is then copied into `.tmp/` so subsequent reads don't need `..` access.
- `sudo dpkg -i` reads the `.deb` from `.tmp/`, not `..`.
- `cd .tmp` keeps cwd inside the repo.
- `/usr/games/xboing` is invoked as a tool, not read as a file.

## Write set

- `include/paths.h` (W1: drop doc line)
- `include/xboing_paths.h` (N1: merge paragraphs)
- `src/paths.c` (N2: collapse return)
- `src/game_init.c` (N3: add `-setup` writable-dir line; re-pad all label columns uniformly so alignment isn't broken — see NB2)
- `Makefile` (new `dogfood` target + help entry)
- `CLAUDE.md` (already modified in working tree on this branch — Tool Usage stay-inside-repo rule from earlier this session)

## Commit structure

Two commits in this PR (per NB3 resolution):

1. **`chore(docs): tool-usage stay-inside-repo rule in CLAUDE.md`** — only the `CLAUDE.md` change. Stand-alone commit so the rule's history is discoverable independent of the cleanup work.
2. **`fix: post-merge #93 cleanup + dogfood wrapper`** — the four findings + the new `make dogfood` target + the `docs/specs/` and `docs/reviews/` artifacts for this branch. (Note: the `docs/specs/` and `docs/reviews/` files for *this* branch will be staged at commit time — they describe the work in this PR, so they ship in commit 2.)

## Success criteria

1. **W1**: `paths_levels_dir_writable` docstring in `include/paths.h` describes only the two implemented branches (env override → `$XDG_DATA_HOME/xboing/levels`). No mention of an unreachable CWD fallback. Behavioral preservation gated by criterion #7 — no test changes required because no behavior changes.
2. **N1**: `include/xboing_paths.h` header comment is one coherent paragraph; no duplicated `-D` explanation.
3. **N2**: `paths_levels_dir_writable` in `src/paths.c` ends with a single-line `return build_path(...)` matching `paths_user_data_dir`'s style. Behavior identical (gated by criterion #7).
4. **N3**: `xboing -setup` output includes a `Levels dir (editor) = ...` line, with all four labels (`Levels dir`, `Levels dir (editor)`, `Sounds dir`, plus any others in the existing block) padded to a uniform column width so `=` columns align. Verified by running `xboing -setup` from a development workstation (display required) and visually checking alignment plus presence of the new label. Headless verification is not required for this criterion.
5. **Dogfood**: `make dogfood` succeeds end-to-end on the developer's machine: builds .deb, copies to `.tmp/`, installs, launches, detects window (or skips window-detection on headless with the alternate alive-after-grace-period check), kills cleanly via captured PID. Passes with no errors. The `.tmp/dogfood-window.txt` artifact contains valid `xwininfo` output when run with a display.
6. **`make check`** clean (full CI parity: format-check + cppcheck-src + cppcheck-tests + markdownlint + ctest debug + ctest asan + dpkg-buildpackage + lintian).
7. **All 51 existing `test_paths.c` tests still pass** — the cleanup is non-functional except for the new `-setup` line.

## Out of scope

- New unit tests for the doc fixes (W1, N1, N2) — these are doc/style changes, not behavior.
- A new test for the `-setup` line (N3) — covered by `make dogfood`'s end-to-end verification + manual eyeball; adding a unit test for `-setup` printf output is overhead for low value.
- Changes to `editor_system.c`, `tests/test_paths.c`, `docs/DESIGN.md` — none of the findings touch these.

## Budget

1 review round. The diff is small (estimated ~30-50 lines across 5 files); peer-review findings should be terse.

## Revision history

- **v1** (2026-04-27): drafted by claude. Sent to gjm for peer review.
- **v1 review** (2026-04-27): gjm — approve-with-changes; 2 blocking + 3 non-blocking. See [peer review](../reviews/2026-04-27-post-merge-93-cleanup-review.md).
- **v2** (2026-04-27): claude — incorporated all findings. Recipe rewritten with explicit PID capture and headless-environment guard; success criterion #1 references #7 for behavior preservation; column-alignment requirement added to N3; commit structure split (CLAUDE.md as `chore(docs):`, cleanup as `fix:`). Delegated to jdc.
- **v3** (2026-04-27): claude — dogfood verification on the implemented Makefile failed with `xwininfo` finding no window. Two recipe defects discovered post-implementation: (1) the launched `xboing` was not `cd`'d into `.tmp/` (jdc's recipe dropped the `cd .tmp` because Make recipe lines run in their own subshell — but that defeats the test, since the repo root has an `assets/` dir that would shadow the install-path lookup); (2) the SDL window title is `XBoing` (capitalized), not `xboing` — my error in the spec recipe. Fixed in the Makefile by wrapping the launch as `( cd .tmp && exec /usr/games/xboing ) & echo $$! > .tmp/dogfood.pid` (the parenthesized subshell forks a backgrounded shell that `cd`s and `exec`s, so `$!` is xboing's actual PID) and changing `xwininfo -name xboing` to `xwininfo -name XBoing`. `make dogfood` now passes end-to-end with window detection. Recorded as a single follow-up commit on the same branch.
