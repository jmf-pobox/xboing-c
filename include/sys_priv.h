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

#endif
