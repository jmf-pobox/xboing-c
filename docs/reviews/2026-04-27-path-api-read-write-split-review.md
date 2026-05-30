# Peer review: Path API read/write split spec

**Spec under review**: [Path API read/write split](../specs/2026-04-27-path-api-read-write-split.md) (v1)
**Reviewer**: gjm (test-engineer)
**Date**: 2026-04-27
**Verdict**: **approve-with-changes**

All findings have been incorporated into spec v2.

---

## Spec issues (blocking)

### B1 — `editor_system.c` has a single `levels_dir` field; struct change missing from write set

`src/editor_system.c:52` shows `char levels_dir[512]` (one field). The spec splits behavior at the call sites (lines 680 for load, 722 for save) but those two lines both read `ctx->levels_dir`. To route them to different paths the struct needs a second field — `levels_dir_writable` or similar — and `editor_system_init` (or wherever the config is injected) needs to accept both. The write set lists `src/editor_system.c` but not `include/editor_system.h`, which must also change because `editor_system_callbacks_t` and the init signature are declared there. Missing `include/editor_system.h` from the write set is a blocking omission.

### B2 — `paths_levels_dir_writable` semantics when `XBOING_LEVELS_DIR` is set are contradicted

The spec says the legacy env override is "highest-precedence" and routes to `_writable` unchanged. But `XBOING_LEVELS_DIR` is a read dir override in the 1996 code (`original/editor.c:912-915`), not a write dir override. If a user sets `XBOING_LEVELS_DIR=/usr/share/xboing/levels` (a read-only system dir) and the writable accessor returns it verbatim, the editor will still get EROFS — exactly the bug being fixed. The spec must decide: does the env override bypass the read/write split or not? If yes, document why and who bears the consequence. If no, the env-override case for `_writable` must explicitly fall through to XDG_DATA_HOME when the override path is not writable. Not deciding this is a semantic gap that will cause jdc to make an arbitrary choice.

---

## Spec issues (non-blocking, recommended)

### N1 — "caller mkdir -p's it before first save" is under-specified

The spec says `editor_system.c` calls mkdir before first save but does not say which function, what error to surface to the user on failure, or what happens if mkdir fails partway (parent exists, leaf doesn't). A note saying "use `mkdir(path, 0755)` after `paths_levels_dir_writable`; on failure call `ctx->cb.on_error`" would eliminate ambiguity.

### N2 — `paths_sounds_dir_readable` is declared but has no corresponding writable split

Sounds are never written by the editor, so no writable accessor is needed. The spec should explicitly say this rather than leaving jdc to infer it — otherwise the parallel structure of the naming invites a `paths_sounds_dir_writable` stub to appear in the implementation.

### N3 — `paths_install_data_dir` null-args test in the test plan is incomplete

The spec says NULL args → `PATHS_NOT_FOUND` defensively. The existing implementation at `src/paths.c:382-384` guards `cfg == NULL || subdir == NULL || buf == NULL || bufsize == 0` but the new `_readable` accessors are delegating to `resolve_data_subdir` which does not guard `cfg == NULL`. The spec should either specify null-cfg behavior consistently for all new accessors or point to a shared defensive wrapper.

---

## Test plan adequacy

The plan is structurally correct. Two gaps:

1. **Writable accessor — env override pointing at a read-only path.** The plan tests "env override returns its value verbatim" but does not test the case where `XBOING_LEVELS_DIR` resolves to a non-writable directory. Whether that case should return the env value anyway or fall through is the B2 issue above — but a test must exist either way to lock the decision.

2. **`_writable` XDG fallback when `XDG_DATA_HOME` is not set.** The test plan covers `$XDG_DATA_HOME/xboing/<subdir>` returned even if dir doesn't exist, but does not specify what `paths_levels_dir_writable` returns when `XDG_DATA_HOME` is also unset (forcing the `$HOME/.local/share` default). Add an explicit test with `xdg_data_home=NULL` to confirm the default expansion fires.

3. **Truncation in `paths_install_data_dir` direct tests.** The plan lists "Tiny buf → `PATHS_TRUNCATED`" but the current implementation at `src/paths.c:387-388` returns `PATHS_TRUNCATED` on snprintf overflow before the opendir call — a test that feeds a buf of size 1 should confirm this ordering, not just that truncation is returned.

---

## Hermetic-test concerns

The existing test suite (TC-09 through TC-30) depends on the real `./levels/` and `./sounds/` directories existing in cwd at test time. This works in the repo tree but is fragile for the new tests.

The spec correctly mandates `mkdtemp(3)` for per-test temp trees. Two risks remain:

1. **Tests using `paths_install_data_dir` directly still need a real directory created under the mkdtemp tree.** If setup creates `<tmpdir>/xboing/levels/` but the opendir check inside `paths_install_data_dir` follows XDG_DATA_DIRS, the test must inject that tmpdir as an XDG_DATA_DIRS entry via `paths_init_explicit`. The spec does not say this explicitly. A CI runner that has `/usr/local/share/xboing/levels` installed will pass even with a buggy injection because opendir finds the system dir first.

2. **`_writable` test for "dir doesn't exist on disk"**: the test must use a path under the mkdtemp tree that does not exist (not under `/nonexistent/home` as in current tests), because some CI runners run as root and `/nonexistent` may be creatable.

---

## Write-set completeness

**Missing**: `include/editor_system.h` — required for the struct field addition and init signature change called out in B1.

Everything else listed is correct. No spurious additions.

---

## Original-source alignment

The alignment is accurate for the read chain. The deviation (write to XDG_DATA_HOME instead of install dir) is correctly identified and attributed to EROFS constraints on modern Linux, and ADR-037 is the right place to record it. No drift found beyond the B2 ambiguity about what "env override at highest precedence" means when applied to the write path — the 1996 source never had a write-path distinction, so there is no original behavior to honor there; the spec should say so rather than extending the 1996 precedence rule by analogy.

---

## Resolution by leader (claude, COO)

All findings incorporated into spec v2:

- **B1**: Added `include/editor_system.h` to write set; struct field split (`levels_dir_readable` + `levels_dir_writable`); `editor_system_create` signature change documented.
- **B2 decision**: `XBOING_LEVELS_DIR` env override applies to BOTH read and write paths. Rationale: the 1996 contract treated the env var as "this is THE dir for everything"; the user setting it opts back into that single-dir model and assumes write-permission responsibility. Documented in spec and ADR-037.
- **N1**: Specified `mkdir(path, 0755)` discipline, recursive `mkdir_p` helper, `cb.on_error` surfacing, save aborts on mkdir failure.
- **N2**: Spec now explicitly states no `paths_sounds_dir_writable` exists.
- **N3**: NULL-cfg defensive guards specified for every new accessor.
- **Test plan gaps**: All three added (env override → read-only path lock test, `XDG_DATA_HOME=NULL` default expansion, truncation-before-opendir).
- **Hermetic concerns**: Test plan now requires mkdtemp tree injected as XDG_DATA_DIRS entry; "dir doesn't exist" tests use mkdtemp paths not `/nonexistent`.
