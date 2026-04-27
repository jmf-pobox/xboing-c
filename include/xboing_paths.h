/*
 * xboing_paths.h -- System-wide install paths for xboing data files.
 *
 * The install location is the **primary** path the binary uses at
 * runtime — every end-user invocation (desktop launcher, systemd unit,
 * `xboing` from any shell) resolves here.  The cwd-relative
 * source-tree paths in each subsystem (e.g. "assets/images") are a
 * **dev fallback** used only when the install isn't present.
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
