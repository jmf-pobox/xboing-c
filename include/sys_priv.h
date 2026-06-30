/*
 * sys_priv.h — setgid-games privilege management.
 *
 * The xboing binary is installed setgid games (Debian Policy 11.5)
 * so it can write the shared /var/games/xboing/scores.dat without
 * being world-writable.  At process start we save the games gid and
 * drop egid to the user's primary gid; the global-scores write path
 * temporarily elevates back, writes, then drops again.
 *
 * All functions are no-ops when the binary is not setgid (e.g.,
 * development builds run from build/) — the elevate/drop calls just
 * setegid() to the same gid the process already had.
 */
#ifndef SYS_PRIV_H
#define SYS_PRIV_H

#include <stdbool.h>
#include <sys/types.h>

/* Call once from main() before any file I/O.  Saves the original
 * egid and drops effective gid to the real gid. */
void sys_priv_init(void);

/* Restore the saved games egid so the next file operation lands
 * with group=games.  Returns 0 on success, -1 with errno set. */
int sys_priv_elevate(void);

/* Drop egid back to the real gid.  Returns 0 on success, -1 with
 * errno set.  Call after each global-file operation completes. */
int sys_priv_drop(void);

/* Nonzero if the binary started setgid — i.e. the egid saved at
 * sys_priv_init differs from the real gid (the Debian setgid-games
 * deployment).  Lets callers skip the elevated global-write path
 * entirely on unprivileged installs (Homebrew, dev builds), where there
 * is no shared /var/games board.  Returns 0 if init has not run. */
int sys_priv_is_setgid(void);

/* True when a shared global high-score board exists for this process:
 * either the binary is setgid games (the Debian /var/games deployment)
 * or an explicit score-file override is configured (score_file_override
 * is a non-empty string, e.g. $XBOING_SCORE_FILE — used by tests and
 * administration).  Single source of truth for the gate that decides
 * whether the global board is read, written, and ranked against; pass
 * cfg->xboing_score_file (NULL is treated as "no override"). */
bool sys_priv_global_board_active(const char *score_file_override);

#endif
