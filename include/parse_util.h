/*
 * parse_util.h — strict integer string parsing.
 *
 * Replaces atoi() at every site that takes untrusted input (CLI args,
 * config files, dialogue input).  atoi("12bogus") silently returns 12
 * and never reports overflow; strtol() with errno + endptr validation
 * rejects partial parses, non-numeric input, and overflow at the parse
 * boundary instead of letting bad values flow into game state.
 *
 * The dialogue NUMERIC mode (.claude/rules/c-code.md exception) is the
 * one allowed atoi caller — its input gate guarantees digits-only.
 */

#ifndef PARSE_UTIL_H
#define PARSE_UTIL_H

#include <stdbool.h>

/*
 * Parse `s` as a signed base-10 integer in the inclusive range [lo, hi].
 *
 * Returns true and writes the parsed value to *out on success.
 * Returns false (leaving *out unchanged) when any of the following hold:
 *   - s is NULL or empty
 *   - s contains characters other than an optional sign and digits
 *   - the parsed value would overflow `int`
 *   - the parsed value is outside [lo, hi]
 *
 * Trailing whitespace and embedded garbage both fail — strict by design.
 */
bool parse_int_in_range(const char *s, int lo, int hi, int *out);

#endif /* PARSE_UTIL_H */
