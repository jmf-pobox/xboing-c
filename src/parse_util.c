/*
 * parse_util.c — see parse_util.h for module overview.
 */

#include "parse_util.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

bool parse_int_in_range(const char *s, int lo, int hi, int *out)
{
    if (!s || !out || s[0] == '\0')
    {
        return false;
    }
    /* strtol() skips leading whitespace, but the header contract
     * promises strict parsing — optional sign then digits only.
     * Reject leading whitespace at the boundary. */
    if (s[0] != '+' && s[0] != '-' && !(s[0] >= '0' && s[0] <= '9'))
    {
        return false;
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0')
    {
        return false;
    }
    if (v < (long)INT_MIN || v > (long)INT_MAX)
    {
        return false;
    }
    if ((int)v < lo || (int)v > hi)
    {
        return false;
    }

    *out = (int)v;
    return true;
}
