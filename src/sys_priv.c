/*
 * sys_priv.c — setgid-games privilege management.  See sys_priv.h.
 */
#include "sys_priv.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Saved at sys_priv_init() and read by sys_priv_elevate/drop.  File-
 * scope: no consumer outside this translation unit needs to chgrp a
 * newly-created file (the global lock directory is setgid, so files
 * created under it inherit group=games).  Promote to extern only if
 * a real cross-module consumer appears. */
static gid_t g_games_gid_saved;
static int g_initialized;

void sys_priv_init(void)
{
    g_games_gid_saved = getegid();
    g_initialized = 1;
    /* On non-setgid binaries egid == rgid already and setegid is a
     * no-op.  On setgid binaries we MUST drop or every file we open
     * for the rest of the session is created group=games. */
    if (setegid(getgid()) != 0 && getegid() != getgid())
    {
        fprintf(stderr,
                "xboing: warning: failed to drop egid (%s); subsequent file "
                "creates may be group=%u for the session\n",
                strerror(errno), (unsigned)g_games_gid_saved);
    }
}

int sys_priv_elevate(void)
{
    /* Fail fast if init was skipped — without it, g_games_gid_saved
     * is indeterminate and setegid(g_games_gid_saved) could land us
     * on any gid the kernel happens to find there.  Better to refuse
     * the elevation and let the caller surface "global write failed"
     * than to silently misbehave. */
    if (!g_initialized)
    {
        fprintf(stderr, "xboing: sys_priv_elevate called before sys_priv_init; refusing\n");
        return -1;
    }
    return setegid(g_games_gid_saved);
}

int sys_priv_drop(void)
{
    if (!g_initialized)
    {
        fprintf(stderr, "xboing: sys_priv_drop called before sys_priv_init; refusing\n");
        return -1;
    }
    int rc = setegid(getgid());
    if (rc != 0 && getegid() != getgid())
    {
        fprintf(stderr,
                "xboing: warning: failed to drop egid after global write (%s); "
                "subsequent file creates may be group=%u\n",
                strerror(errno), (unsigned)g_games_gid_saved);
    }
    return rc;
}

int sys_priv_is_setgid(void)
{
    return g_initialized && g_games_gid_saved != getgid();
}
