# Building XBoing

This document covers building, toolchain, quality gates, and packaging.
Read it in full before writing any code.

## Build Commands

```bash
cmake --preset debug                          # Configure (build/, Debug)
cmake --build build                           # Build the game and all tests
ctest --test-dir build --output-on-failure    # Run unit + integration tests
./build/xboing                                # Run the game
```

### Sanitizer Build (ASan + UBSan)

```bash
cmake --preset asan                           # Configure (build-asan/)
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

### Debian Package

```bash
sudo apt install build-essential devscripts debhelper cmake lintian \
    libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev libcmocka-dev
dpkg-buildpackage -us -uc -b
sudo dpkg -i ../xboing_*.deb        # installs /usr/games/xboing
```

### Dependencies (Source Build)

`libsdl2-dev`, `libsdl2-image-dev`, `libsdl2-mixer-dev`,
`libsdl2-ttf-dev`, `libcmocka-dev`, `pkg-config`.

### CLion

The `debug` and `asan` CMake presets in `CMakePresets.json` are
pre-wired — open the project, pick a preset, build. Do **not** run
`cmake .` in the repo root (it pollutes the source tree).

### Legacy Build

The 1996 Xlib build (`original/Makefile`, `original/xboing`) is
preserved verbatim in `original/` for reference. It is not the active
build. The top-level `Makefile` is the modern wrapper around CMake.

## Makefile

**`make` is the source of truth.** Run `make help` to see every
target. Do not re-derive flag combinations from CI YAML — call the
wrapper.

Key targets:

| Target | Purpose |
|--------|---------|
| `make build` | Build the game and all tests (debug) |
| `make test` | Run the full ctest suite (debug build) |
| `make asan` | Configure + build the sanitizer preset |
| `make asan-test` | Run ctest under ASan + UBSan |
| `make run` | Build and run the game |
| `make check` | Run every CI gate locally (see Quality Gates) |
| `make format` | Apply clang-format in-place |
| `make format-check` | Check formatting without modifying |
| `make cppcheck-src` | Static analysis on src/ |
| `make lint` | Markdown lint |
| `make deb` | Build a Debian package |
| `make deb-lint` | Build .deb + run lintian |
| `make dogfood` | Install .deb, launch, verify window opens |
| `make golden-screen` | Capture original goldens (see docs/TESTING.md) |
| `make golden-all` | Capture all attract screen goldens |
| `make modern-screen` | Capture modern screenshots |
| `make visual-check` | LLM-based visual comparison |

## Toolchain

| Tool | Purpose | Install |
|------|---------|---------|
| **gcc / clang** | Compilation with strict warnings | `apt install gcc clang` |
| **CMake** | Build system | `apt install cmake` |
| **clang-format** | Code formatting | `apt install clang-format` |
| **clang-tidy** | Deep semantic static analysis | `apt install clang-tidy` |
| **cppcheck** | Syntactic static analysis | `apt install cppcheck` |
| **CMocka** | Unit test framework | `apt install libcmocka-dev` |
| **Valgrind** | Memory debugging | `apt install valgrind` |
| **shellcheck** | Shell script linting | `apt install shellcheck` |
| **ImageMagick** | Screenshot capture (`import`) | `apt install imagemagick` |
| **xdotool** | X11 keystroke injection — native X11 sessions only (`XDG_SESSION_TYPE=x11`). Under XWayland, Mutter blocks focus transfer; xdotool delivers no keys to SDL2 windows. See `docs/TESTING.md` for the savegame-fixture + `-load` alternative that avoids key injection entirely. | `apt install xdotool` |
| **ydotool** (Wayland) | uinput-based keystroke injection — works regardless of compositor focus. Required only if you need to drive the modern SDL2 binary via keystrokes on a Wayland session. | `apt install ydotool` |

## Quality Gates

Run `make check` before every PR. It runs all 6 gates in sequence:

1. `format-check` — clang-format dry-run
2. `cppcheck` — static analysis on src/ and tests/
3. `lint` — markdownlint on all .md files
4. `test` — ctest debug build (all unit + integration tests)
5. `asan-test` — ctest ASan + UBSan build
6. `deb-lint` — dpkg-buildpackage + lintian

Zero warnings, zero errors, all tests green. Never let CI catch
something you could have caught locally.

### Compiler Warnings Policy

Base warning set for all translation units:

```text
-Wall -Wextra -Wpedantic -Werror
-Wconversion -Wshadow -Wdouble-promotion
-Wformat=2 -Wformat-overflow=2
-Wnull-dereference -Wuninitialized
-Wstrict-prototypes -Wold-style-definition
```

For legacy files not yet modernized, suppress specific warnings
per-file in CMakeLists.txt rather than globally weakening the policy.
Track each suppression as a bead to resolve.

### Sanitizer Builds

| Sanitizer | What it catches | Flag |
|-----------|-----------------|------|
| AddressSanitizer (ASan) | Buffer overflows, use-after-free, double-free, stack overflow | `-fsanitize=address` |
| UndefinedBehaviorSanitizer (UBSan) | Integer overflow, null deref, alignment, shift errors | `-fsanitize=undefined` |
| MemorySanitizer (MSan) | Reads of uninitialized memory (clang only, Linux only) | `-fsanitize=memory` |

ASan + UBSan run in CI on every PR. MSan and Valgrind are periodic
deep checks. Sanitizers are not optional — they are the primary
safety net for a 30-year-old C codebase.

## CI Workflows

| Workflow | What it runs |
|----------|-------------|
| `lint.yml` | clang-format, cppcheck |
| `test.yml` | Matrix build: Debug + ASan, ctest |
| `docs.yml` | markdownlint |
