/*
 * xboing_paths.h -- System-wide install paths for xboing data files.
 *
 * Asset resolution at runtime tries, in order: $XDG_DATA_DIRS lookup
 * (handles the common /usr and /usr/local prefixes for free), the
 * compile-time XBOING_INSTALLED_*_DIR macros below (safety net for
 * unusual installs), then the cwd-relative source-tree default in
 * each subsystem (dev fallback).
 *
 * XBOING_DATA_DIR is overridable at compile time via -D.  Because the
 * derived macros below use string-literal concatenation (e.g.
 * XBOING_DATA_DIR "/images"), the override must itself be a quoted C
 * string literal — pass `-DXBOING_DATA_DIR="\"/opt/xboing/share\""` to
 * a raw compiler invocation, or set it from a CMake string variable
 * (see target_compile_definitions in CMakeLists.txt, which formats
 * the override as XBOING_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/xboing"
 * — the surrounding quotes survive into the preprocessor).
 *
 * XBOING_DATA_DIR can be overridden at compile time via -D so that
 * non-system installs (e.g. a relocatable bundle) point at the right
 * location.  Default matches Debian Policy: data files in
 * /usr/share/<package>/.
 *
 * Layout under XBOING_DATA_DIR (matches what CMakeLists.txt installs):
 *   levels/        — 80 .data level files
 *   sounds/        — *.wav sound effects
 *   fonts/         — TTF fonts (symlinks to fonts-liberation in the .deb)
 *   images/        — PNG sprite sheets organized by category
 *   docs/          — bundled documentation (problems.doc)
 */

#ifndef XBOING_PATHS_H
#define XBOING_PATHS_H

#ifndef XBOING_DATA_DIR
#define XBOING_DATA_DIR "/usr/share/xboing"
#endif

#define XBOING_INSTALLED_IMAGES_DIR XBOING_DATA_DIR "/images"
#define XBOING_INSTALLED_FONTS_DIR XBOING_DATA_DIR "/fonts"
#define XBOING_INSTALLED_SOUNDS_DIR XBOING_DATA_DIR "/sounds"
#define XBOING_INSTALLED_LEVELS_DIR XBOING_DATA_DIR "/levels"

#endif /* XBOING_PATHS_H */
