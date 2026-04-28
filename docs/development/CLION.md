# CLion + JetBrains MCP

Findings from a live introspection of this project on 2026-04-27 using the JetBrains MCP server, plus the resulting fixes to the IDE config. Captures what CLion sees when it opens xboing-c, what was wrong with the run configurations, and which MCP tools are useful for an LLM agent working alongside the IDE.

## IDE project setup

### Identity

| Field | Value |
| --- | --- |
| IDE | CLion `CL-261.23567.135` |
| OS | Linux 6.17.0-1017-oem (amd64) |
| Module | single CPP module `xboing-c` (`.idea/xboing-c.iml`) |
| VCS | Git, one root at project root |
| Build system | CMake (auto-reload enabled) |

### `.idea/` is tracked (as of this commit)

The previous setup gitignored `.idea/` wholesale, so the only IDE config any contributor saw was the auto-derived defaults. As of 2026-04-27 the outer `.gitignore` no longer excludes `.idea/`, and the inner `.idea/.gitignore` does the work — excluding the per-user state files (`workspace.xml`, `shelf/`, `usage.statistics.xml`, `tasks.xml`, `dictionaries/`, etc.) while letting project-level config (run configurations, code style, modules, VCS mapping, inspection severities) ride with the repo.

Practical consequence: a fresh clone opens in CLion with the working `xboing` run config, the project code style, and the customized C inspection severities pre-applied. No "first-run setup" needed.

### CMake profiles

CLion mirrors `CMakePresets.json` 1:1:

| Profile | Build type | Generation dir | Preset |
| --- | --- | --- | --- |
| debug | Debug | `build/` | `--preset "debug"` |
| asan | Debug | `build-asan/` | `--preset "asan"` |
| install | Release | `build-install/` | `--preset "install"` |

Switching profile in CLion is equivalent to running `cmake --preset <name>` from the shell — no IDE-private build flags drift in.

### Toolchain

- Type: `SYSTEM_UNIX_TOOLSET` (CLion's "Default")
- CMake: `4.2.2` from the snap (`/snap/clion/439/bin/cmake/linux/x64/bin/cmake`) — this is CLion's bundled CMake, not the system one
- Debugger: GDB `16.3` from the snap (`/snap/clion/439/bin/gdb/linux/x64/bin/gdb`)
- Compiler: GCC at `/usr/bin/cc` (per `get_compiler_info` on `src/game_render.c`)

> Note: `get_diagnostic_info` mis-labels the GDB entry as a second `cmake` (same `name` field). The `executablePath` is the source of truth — read paths, not names.

### Compiler invocation seen by code analysis

For `src/game_render.c` in the `xboing | Debug` configuration, the IDE's language engine uses:

- Define: `XBOING_DATA_DIR="/usr/local/share/xboing"`
- Project includes: `include/`, `src/`
- System includes (resolved from SDL2 + transitive deps): `SDL2`, `libpng16`, `webp`, `harfbuzz`, `freetype2`, `glib-2.0`, `opus`, `pipewire-0.3`, `spa-0.2`, `dbus-1.0`, `libinstpatch-2`
- Warnings: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion -Wformat=2 -Wnull-dereference -Wuninitialized -Wstrict-prototypes -Wold-style-definition -Wformat-overflow=2 -Werror`
- Other: `-g -fdiagnostics-color=always`

This matches the `CLAUDE.md` warning policy. CLion inspections will flag the same things `gcc -Werror` would, plus IntelliJ's own C/C++ analyzers.

## Run configurations — the breakage and the fix

### What was wrong (as discovered 2026-04-27)

CLion reported **85** run configurations. The actual CMake build system defines:

- **1** executable: `add_executable(xboing ...)` at `CMakeLists.txt:506` (the SDL2 game)
- **~46** test executables under `tests/` (CMocka)
- **~33** static libraries (`add_library(... STATIC ...)`) — `ball_system`, `block_system`, `sdl2_renderer`, etc.

Two concrete defects:

1. **Stale committed run config for a non-existent target.** `.idea/runConfigurations/xboing_sdl2.xml` referenced `TARGET_NAME="xboing_sdl2"`. That target was renamed to `xboing` in commit `f85c369` (PR #91, "rename binary to xboing, drop install symlink") — the run config was missed. Selecting it would fail the build-before-run step.
2. **~33 phantom run configs for libraries.** CLion's "create run configurations for all CMake targets" feature auto-generated entries for every static library. Static libraries cannot be run. They sit in `workspace.xml` polluting the run-config picker.

### What was fixed

Round 1 (initial CLION.md commit, 2026-04-27):

- Deleted `.idea/runConfigurations/xboing_sdl2.xml`.
- Added `.idea/runConfigurations/xboing.xml` pointing at the real `xboing` target, with `WORKING_DIR=$PROJECT_DIR$` and build-before-run enabled.
- The `All CTest` config (already correct) is unchanged.

Round 2 (same day, follow-up cleanup):

- Disabled the IDE-global toggle "Create run configurations for all targets when CMake project is loaded" (Settings → Advanced Settings).
- Closed CLion, then surgically rewrote the `RunManager` and `CMakeRunConfigurationManager` blocks in `.idea/workspace.xml` to drop the 83 cached phantom configs (one per static library and test executable). Both blocks now reference only the real `xboing` target. workspace.xml shrank 747 → 158 lines.
- Added two more committed configs under `.idea/runConfigurations/`: `xboing-asan.xml` and `All_CTest_asan.xml` — same targets as the debug-pinned configs but `CONFIG_NAME="asan"`, so sanitizer runs are one-click without flipping the global build profile.

### How to keep it clean

The phantom regeneration is driven by one IDE-global setting. Keep it off:

> Settings → Advanced Settings → "Create run configurations for all targets when CMake project is loaded" → **off**.

If that toggle is on, every CMake reload re-creates one auto-detected run config per CMake target (~80 of them in this repo). They cache in `.idea/workspace.xml`, which is gitignored, so the noise stays per-user — but the dropdown gets unusable and `get_run_configurations` returns junk.

Two failure modes worth knowing:

1. **The toggle is per-IDE-installation, not per-project.** It lives in `~/.config/JetBrains/CLion*/options/advancedSettings.xml`. Once flipped, every project benefits; until flipped, every project regenerates phantoms.
2. **CLion writes `advancedSettings.xml` on shutdown.** If you flip the toggle and *then* edit `workspace.xml` from outside, CLion's auto-save will overwrite both. Always close CLion before any direct workspace.xml surgery.

### Run configurations after the fix

Four committed configs:

| File | Purpose |
| --- | --- |
| `.idea/runConfigurations/xboing.xml` | Run the SDL2 game on profile `debug` (`WORKING_DIR=$PROJECT_DIR$`, build-before-run) |
| `.idea/runConfigurations/xboing-asan.xml` | Same target as above but pinned to profile `asan` — one-click sanitizer run from the IDE |
| `.idea/runConfigurations/All_CTest.xml` | Full test suite on profile `debug` (`--output-on-failure`, working dir `$CMakeCurrentLocalGenerationDir$`, `TEST_MODE="SUITE_TEST"`, `EXPLICIT_BUILD_TARGET_NAME="all"`) |
| `.idea/runConfigurations/All_CTest_asan.xml` | Same as above but profile `asan` — runs the entire CMocka suite under ASan + UBSan |

All four expose `supportsDynamicLaunchOverrides: true`, which means the MCP `execute_run_configuration` tool can pass one-time `programArguments` / `workingDirectory` / `envs` overrides without mutating the config file.

### Troubleshooting: "why is nothing in my run-config dropdown?"

Three traps to know about:

1. **CMake profiles ≠ run configurations.** The selector showing `debug`/`asan`/`install` is the *build profile* — what CLion will compile against. Run configurations live in a separate dropdown (the "Run/Debug Configurations" widget). Confusing the two leads to "I selected a profile, where's my run target?"
2. **The toolbar dropdown shows only saved run configs.** Auto-detected CMake-target configs (the ones cached in `workspace.xml`) do *not* appear in the dropdown by default — only configs persisted as XML under `.idea/runConfigurations/` show up. So the MCP server reporting 85 configs while your dropdown shows 1 is consistent: 84 of them are auto-detected shadow entries, not saved.
3. **CLion silently hides run configs whose CMake target can't be resolved.** If a saved XML points at a renamed/deleted target (as `xboing_sdl2.xml` did here), CLion drops it from the dropdown with no warning. After any rename in `CMakeLists.txt`, audit `.idea/runConfigurations/` for `TARGET_NAME` mismatches.

If a freshly committed run-config XML doesn't appear in the dropdown:

> File → Reload All from Disk (Ctrl+Alt+Y), or restart the IDE.

CLion caches the run-config list in memory and only re-reads `.idea/runConfigurations/` on project load or explicit reload.

## JetBrains MCP server

### What it is

A JetBrains-shipped MCP server that exposes the running IDE's services to an LLM. Wired up via the user's Claude config — the project's `.mcp.json` is empty (`{ "mcpServers": {} }`). No project-side setup required: if CLion is open on this project, the tools are live.

### Tool taxonomy (the useful subset)

The server surfaces ~50 tools. Grouped by what they actually do better than shell tools:

**Project introspection** — answers questions you can't easily get from the filesystem:

| Tool | Use case |
| --- | --- |
| `get_project_modules` | Module count and types (`CPP_MODULE` here) |
| `get_all_open_file_paths` | What the user is *currently* looking at |
| `get_run_configurations` | Live run/test configs, with override capability flag |
| `get_compiler_info` | Exact compiler invocation for a file (defines, includes, switches) — the IDE-resolved version, not raw `compile_commands.json` |
| `get_diagnostic_info` | CMake profiles, toolchain, debugger, environment kind |
| `get_repositories` | VCS roots (multi-repo detection) |
| `list_directory_tree` | `tree`-style directory dump scoped to project root |

**Semantic over textual** — leverages the IDE's symbol index:

| Tool | Use case | Beats... |
| --- | --- | --- |
| `get_file_problems` | Run IntelliJ inspections (clang-tidy + IntelliJ's own analyzers) on a single file | Running `clang-tidy` from shell |
| `get_symbol_info` | Resolve a symbol to its definition with type info | `grep` for the name |
| `search_symbol` | Symbol search across project | `grep -rn 'foo('` |
| `search_in_files_by_regex` | Project-aware regex search | `grep -rE` |
| `rename_refactoring` | Real refactor (call sites, headers, etc.) | `sed -i` |

**Build / run drivers** — useful for keeping the IDE's incremental build state warm:

| Tool | Use case |
| --- | --- |
| `build_project` | Trigger an IDE build (uses the active CMake profile) |
| `execute_run_configuration` | Run a named config, optionally with one-shot arg/env overrides |
| `execute_terminal_command` | Run a shell command in the IDE's terminal |

**Inspection / quality**:

| Tool | Use case |
| --- | --- |
| `run_inspection_kts` | Run an arbitrary IntelliJ inspection programmatically |
| `generate_inspection_kts_api` / `_examples` | Discover what inspections are available |

### What works well for this project

- **Pre-commit polish.** Run `get_file_problems` on a file you just edited before committing — it catches issues raw `clang-tidy` misses (IntelliJ has its own C/C++ analyzers layered on top).
- **"Where is the user?"** `get_all_open_file_paths` answers without asking.
- **Confirming the analysis context.** `get_compiler_info` is the cleanest way to verify what defines and includes the IDE is using to type-check a file. Useful when CMake changes don't seem to land.
- **Test triage.** `get_run_configurations` then `execute_run_configuration` lets the agent run a single CMocka test by name (e.g. `test_ball_math`) without needing to remember its path under `build/`.

### What doesn't work / caveats

- **`get_project_dependencies` returns `[]`.** It tracks Maven/Gradle/etc. style declared deps. C projects don't have those — system libs and `find_package` results don't show up. Use `get_compiler_info` for the actual include picture.
- **`get_diagnostic_info` GDB mis-label.** GDB shows up under `tools` with `name: "cmake"` (apparent JetBrains bug). Read `executablePath`, ignore `name`.
- **`get_repositories` is shallow.** Returns only Git roots registered with the IDE, not git submodules or worktrees. Cross-check with `git -C <path> worktree list` if you need the full picture.
- **`get_run_configurations` reflects whatever CLion has loaded** — including the auto-generated library noise described above. The list is not the same as the set of actually committed configs.
- **No headless mode.** All these tools require CLion to be open on the project. CI cannot use them. They are agent-with-IDE-attached tools, not build-server tools.

### When to use shell instead

| Want to... | Use |
| --- | --- |
| Find a string literal in any file | `grep` (faster, no IDE round-trip) |
| List files by name pattern | `find` / `Glob` |
| Run the actual build | `cmake --build build` (parity with CI) |
| Run all tests on CI | `ctest --test-dir build --output-on-failure` |
| Re-format on commit | `clang-format` directly |

The MCP server is a *companion* to shell tools, not a replacement. Use it for IDE-resolved facts (compiler analysis context, inspections, symbols, run configs); use shell for the build, the tests, and bulk file work.

## References

- `CMakePresets.json` — source of truth for CLion's CMake profiles
- `.clang-format` — formatting rules CLion's project-level style should mirror
- `.idea/runConfigurations/{xboing,xboing-asan,All_CTest,All_CTest_asan}.xml` — the four run configs persisted as files (debug + asan pairs for game and test suite)
- `.idea/.gitignore` — excludes per-user state from the now-tracked `.idea/` directory
- `CLAUDE.md` "Toolchain" and "Build Commands" sections — shell-side equivalents
