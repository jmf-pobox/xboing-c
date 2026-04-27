# Peer review: Post-merge cleanup of PR #93 + dogfood wrapper

**Spec under review**: [Post-merge cleanup of PR #93 + dogfood wrapper](../specs/2026-04-27-post-merge-93-cleanup.md) (v1)
**Reviewer**: gjm (test-engineer)
**Date**: 2026-04-27
**Verdict**: **approve-with-changes**

All findings have been incorporated into spec v2.

## Blocking issues

### B1 — `make dogfood` recipe: multi-command recipe lines and racy kill

The spec wrote `@cd .tmp && /usr/games/xboing &` as a single recipe line. Two problems:

- Make runs each recipe line in its own subshell. `cd .tmp` has no lasting effect on subsequent lines.
- `pkill -x xboing` later in the recipe is a race against any pre-existing `xboing` instance the user might have running — would silently kill the wrong process.

The recipe must capture the launched PID explicitly:

```makefile
/usr/games/xboing & echo $$! > .tmp/dogfood.pid
```

and kill via `kill "$$(cat .tmp/dogfood.pid)"` rather than `pkill -x xboing`.

### B2 — `xwininfo -name xboing` requires a live X display

On machines where `$DISPLAY` is unset (VMs, remote SSH without X forwarding, CI runners), `xwininfo` exits non-zero immediately with an X connection error — not because `xboing` failed to launch, but because there is no display. The spec gives no guidance for headless environments. Without a guard, `make dogfood` becomes an unconditional failure if ever wired into CI.

The recipe must check `[ -n "$DISPLAY" ]` (or `[ -n "$WAYLAND_DISPLAY" ]`) before invoking `xwininfo`, and document that headless environments verify install-without-crash rather than window detection.

## Non-blocking issues

### NB1 — Success criterion #1 doesn't link to behavioral-preservation gate

The W1 fix is correct (drop the unreachable doc line). Success criterion #1 says "describes only the two implemented branches" but doesn't state that criterion #7 ("all 51 existing tests still pass") is the gate for behavioral preservation. Make the linkage explicit.

### NB2 — N3 output format: column alignment will break

The spec proposes:

```text
  Levels dir          = /usr/share/xboing/levels
  Levels dir (editor) = /home/user/.local/share/xboing/levels
```

The existing code at `game_init.c:147` uses `"  Levels dir         = %s\n"` (9 trailing spaces). `Levels dir (editor)` is 7 chars longer; alignment with existing lines will be off unless jdc re-pads all labels together. The spec doesn't call this out. Re-align the full block when adding the new line.

### NB3 — `CLAUDE.md` riding along: convenience argument, not principled

The spec justifies the `CLAUDE.md` inclusion as "already modified in working tree." That's convenience, not principle. If the `CLAUDE.md` change is the Tool Usage stay-inside-repo rule, it's a `chore:` topic, not a `fix:` topic. It will appear in the diff of a PR whose title is about post-merge cleanup of PR #93. Reviewers will reasonably ask why it's here.

Either give jdc a separate commit for `CLAUDE.md` (same PR is fine, same commit is not clean), or acknowledge the mixed commit as an accepted tradeoff with a brief justification.

## Write-set completeness

Complete. No spurious entries. The five source files plus `CLAUDE.md` cover exactly the four findings and the dogfood gap. `tests/test_paths.c` is correctly excluded — N2's collapse is a style change with identical observable behavior, already covered by the 51 existing tests.

## Out-of-scope honesty

Defensible for W1, N1, N2. The "no unit test for N3 `-setup` output" exclusion is borderline: the output of `-setup` is a human-readable string with no machine consumer, so a printf-output unit test would be brittle and low-value. The dogfood verification is the right gate. The out-of-scope rationale holds.

## Success criteria measurability

Criteria 1-3 and 5-7 are concretely verifiable. Criterion 4 ("Verified by spawning `xboing -setup`... and grepping the output") is correct but depends on a display being available — same environment gap as B2. The spec should note that criterion 4 can be verified from a development workstation, not from CI.

## Resolution by leader (claude, COO)

All findings incorporated into spec v2:

- **B1**: Recipe rewritten with explicit PID capture (`echo $$! > .tmp/dogfood.pid`) and targeted kill (`kill "$$(cat .tmp/dogfood.pid)"`). No `pkill -x xboing`.
- **B2**: Conditional probe — `if [ -n "$$DISPLAY" ] || [ -n "$$WAYLAND_DISPLAY" ]`. Headless path skips `xwininfo` and only verifies the process started without immediate crash.
- **NB1**: Success criterion #1 now references criterion #7 explicitly for behavioral preservation.
- **NB2**: Spec now directs jdc to re-pad all `-setup` labels uniformly when adding the editor-save line.
- **NB3**: Spec now requires two commits in this PR: `chore(docs): tool-usage stay-inside-repo rule` for the `CLAUDE.md` change, and `fix: post-merge #93 cleanup + dogfood wrapper` for the actual cleanup.
