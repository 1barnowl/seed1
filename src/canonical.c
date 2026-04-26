#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <openssl/evp.h>

#include "seed.h"

void sha256_bytes(const uint8_t *data, size_t len, uint8_t out[32]) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned int hash_len = 32;
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, out, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void sha256_hex(const uint8_t *data, size_t len, char out[SHA256_HEX_LEN]) {
    uint8_t hash[32];
    sha256_bytes(data, len, hash);
    hex_encode(hash, 32, out);
}

void sha256_file(const char *path, char out[SHA256_HEX_LEN]) {
    FILE *f = fopen(path, "rb");
    if (!f) { memset(out, '0', SHA256_HEX_LEN - 1); out[SHA256_HEX_LEN-1] = '\0'; return; }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned int hash_len = 32;
    uint8_t      hash[32];
    uint8_t      buf[4096];
    size_t       n;

    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        EVP_DigestUpdate(ctx, buf, n);
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    fclose(f);

    hex_encode(hash, 32, out);
}
