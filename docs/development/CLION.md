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

- Deleted `.idea/runConfigurations/xboing_sdl2.xml`.
- Added `.idea/runConfigurations/xboing.xml` pointing at the real `xboing` target, with `WORKING_DIR=$PROJECT_DIR$` and build-before-run enabled.
- The `All CTest` config (already correct) is unchanged.

### What's still untidy

The auto-generated library configs in `workspace.xml` are local user state and not committed. They stay until disabled at the IDE level:

> Settings → Advanced Settings → "Create run configurations for all targets when CMake project is loaded" → uncheck.
>
> Then: Run → Edit Configurations → delete the phantom library entries.

If you don't disable that toggle, every fresh CMake reload regenerates the noise.

### Run configurations after the fix

Two committed configs (the only ones that should be):

| File | Purpose |
| --- | --- |
| `.idea/runConfigurations/xboing.xml` | Run the SDL2 game (target `xboing`, `WORKING_DIR=$PROJECT_DIR$`, build-before-run) |
| `.idea/runConfigurations/All_CTest.xml` | Run the full test suite (`--output-on-failure`, working dir `$CMakeCurrentLocalGenerationDir$`) |

Both have `supportsDynamicLaunchOverrides: true`, which means the MCP `execute_run_configuration` tool can pass one-time `programArguments` / `workingDirectory` / `envs` overrides without mutating the config file.

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
- `.idea/runConfigurations/{xboing,All_CTest}.xml` — the only two run configs persisted as files
- `.idea/.gitignore` — excludes per-user state from the now-tracked `.idea/` directory
- `CLAUDE.md` "Toolchain" and "Build Commands" sections — shell-side equivalents
