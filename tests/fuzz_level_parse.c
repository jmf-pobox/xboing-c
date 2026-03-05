/*
 * fuzz_level_parse.c — libFuzzer harness for level_system_load_file().
 *
 * Feeds arbitrary byte sequences to the level file parser to catch
 * crashes, buffer overflows, and undefined behavior.  Designed to run
 * with ASan + UBSan enabled.
 *
 * Build: clang only (requires -fsanitize=fuzzer).
 * Run:   ./build/tests/fuzz_level_parse -max_total_time=30
 *
 * NOT registered as a ctest target — run manually or in CI fuzzing jobs.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "level_system.h"

/* No-op callback — we just care that the parser doesn't crash */
static void on_add_block(int row, int col, int block_type, int counter_slide, void *ud)
{
    (void)row;
    (void)col;
    (void)block_type;
    (void)counter_slide;
    (void)ud;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Write fuzz input to a temp file */
    char path[] = "/tmp/fuzz_level_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return 0;

    /* Write in chunks to handle large inputs */
    size_t written = 0;
    while (written < size)
    {
        ssize_t n = write(fd, data + written, size - written);
        if (n <= 0)
            break;
        written += (size_t)n;
    }
    close(fd);

    /* Create a fresh level system for each fuzz input */
    level_system_callbacks_t cb = {.on_add_block = on_add_block};
    level_system_status_t status;
    level_system_t *ctx = level_system_create(&cb, NULL, &status);
    if (ctx)
    {
        /* Parse — may return error status, but must not crash */
        level_system_load_file(ctx, path);

        /* Query results — must not crash even after parse failure */
        (void)level_system_get_title(ctx);
        (void)level_system_get_time_bonus(ctx);

        level_system_destroy(ctx);
    }

    unlink(path);
    return 0;
}
