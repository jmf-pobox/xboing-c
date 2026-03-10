/*
 * fuzz_config_io.c — libFuzzer harness for config_io_read().
 *
 * Feeds arbitrary byte sequences to the TOML config parser to catch
 * crashes, buffer overflows, and undefined behavior.
 *
 * Build: clang only (requires -fsanitize=fuzzer).
 * Run:   ./build/tests/fuzz_config_io -max_total_time=30
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config_io.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char path[] = "/tmp/fuzz_config_XXXXXX";
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

    config_data_t data_out;
    config_io_init(&data_out);
    config_io_read(path, &data_out);

    unlink(path);
    return 0;
}
