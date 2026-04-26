#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "seed.h"

static void store_path(const char *store_dir, const char *hex,
                        char *out, size_t cap) {
    /* git-style: first 2 chars as subdir, rest as filename */
    char subdir[MAX_PATH_LEN];
    snprintf(subdir, sizeof(subdir), "%s/%.2s", store_dir, hex);
    mkdir(subdir, 0700);
    snprintf(out, cap, "%s/%.2s/%s", store_dir, hex, hex + 2);
}

int store_init(const char *store_dir) {
    return mkdir_p(store_dir, 0700);
}

int store_put(const char *store_dir, const uint8_t *data, size_t len,
              char hex_out[SHA256_HEX_LEN]) {
    sha256_hex(data, len, hex_out);

    char path[MAX_PATH_LEN];
    store_path(store_dir, hex_out, path, sizeof(path));

    if (file_exists(path)) return 0;  /* already stored (content-addressed) */

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len ? 0 : -1;
}

int store_get(const char *store_dir, const char *hex,
              uint8_t *buf, size_t cap, size_t *out_len) {
    char path[MAX_PATH_LEN];
    store_path(store_dir, hex, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    if (out_len) *out_len = n;
    return 0;
}

int store_has(const char *store_dir, const char *hex) {
    char path[MAX_PATH_LEN];
    store_path(store_dir, hex, path, sizeof(path));
    return file_exists(path);
}
