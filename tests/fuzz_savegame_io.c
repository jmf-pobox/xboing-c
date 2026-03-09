/*
 * fuzz_savegame_io.c — libFuzzer harness for savegame_io_read().
 *
 * Feeds arbitrary byte sequences to the JSON savegame parser to catch
 * crashes, buffer overflows, and undefined behavior.
 *
 * Build: clang only (requires -fsanitize=fuzzer).
 * Run:   ./build/tests/fuzz_savegame_io -max_total_time=30
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "savegame_io.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char path[] = "/tmp/fuzz_savegame_XXXXXX";
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

    savegame_data_t data_out;
    savegame_io_init(&data_out);
    savegame_io_read(path, &data_out);

    unlink(path);
    return 0;
}
