/*
 * fuzz_highscore_io.c — libFuzzer harness for highscore_io_read().
 *
 * Feeds arbitrary byte sequences to the JSON highscore parser to catch
 * crashes, buffer overflows, and undefined behavior.
 *
 * Build: clang only (requires -fsanitize=fuzzer).
 * Run:   ./build/tests/fuzz_highscore_io -max_total_time=30
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "highscore_io.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char path[] = "/tmp/fuzz_highscore_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return 0;

    size_t written = 0;
    while (written < size)
    {
        ssize_t n = write(fd, data + written, size - written);
        if (n <= 0)
            break;
        written += (size_t)n;
    }
    close(fd);

    highscore_table_t table;
    highscore_io_init_table(&table);
    highscore_io_read(path, &table);

    unlink(path);
    return 0;
}
