#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#include "seed.h"

static int save_key(EVP_PKEY *pkey, const char *dir) {
    char priv_path[MAX_PATH_LEN];
    char pub_path [MAX_PATH_LEN];
    snprintf(priv_path, sizeof(priv_path), "%s/seed_priv.pem", dir);
    snprintf(pub_path,  sizeof(pub_path),  "%s/seed_pub.pem",  dir);

    /* Private key */
    BIO *bio = BIO_new_file(priv_path, "w");
    if (!bio) return -1;
    chmod(priv_path, 0600);
    PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL);
    BIO_free(bio);

    /* Public key */
    bio = BIO_new_file(pub_path, "w");
    if (!bio) return -1;
    PEM_write_bio_PUBKEY(bio, pkey);
    BIO_free(bio);

    return 0;
}

static EVP_PKEY *load_key(const char *dir) {
    char priv_path[MAX_PATH_LEN];
    snprintf(priv_path, sizeof(priv_path), "%s/seed_priv.pem", dir);

    BIO *bio = BIO_new_file(priv_path, "r");
    if (!bio) return NULL;

    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return pkey;
}

static int extract_pubkey(EVP_PKEY *pkey, uint8_t pubkey[PUBKEY_BYTES]) {
    size_t len = PUBKEY_BYTES;
    return EVP_PKEY_get_raw_public_key(pkey, pubkey, &len) == 1 ? 0 : -1;
}

int keymgmt_init(SeedState *s) {
    char priv_path[MAX_PATH_LEN];
    snprintf(priv_path, sizeof(priv_path), "%s/seed_priv.pem", s->keys_dir);

    EVP_PKEY *pkey = NULL;

    if (file_exists(priv_path)) {
        pkey = load_key(s->keys_dir);
        if (!pkey) {
            fprintf(stderr, "[keymgmt] failed to load existing key — regenerating\n");
        } else {
            seed_log(s, "loaded Ed25519 signing key from %s", s->keys_dir);
        }
    }

    if (!pkey) {
        /* Generate new Ed25519 keypair */
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
        if (!ctx) return -1;
        if (EVP_PKEY_keygen_init(ctx) <= 0) { EVP_PKEY_CTX_free(ctx); return -1; }
        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) { EVP_PKEY_CTX_free(ctx); return -1; }
        EVP_PKEY_CTX_free(ctx);

        if (save_key(pkey, s->keys_dir) != 0) {
            EVP_PKEY_free(pkey);
            return -1;
        }
        seed_log(s, "generated new Ed25519 keypair -> %s", s->keys_dir);
    }

    s->signing_key = pkey;
    extract_pubkey(pkey, s->pubkey_bytes);

    char pubhex[65];
    hex_encode(s->pubkey_bytes, PUBKEY_BYTES, pubhex);
    seed_log(s, "pubkey: %s", pubhex);

    return 0;
}

int keymgmt_sign(SeedState *s, const uint8_t *msg, size_t len,
                 uint8_t sig[SIG_BYTES]) {
    if (!s->signing_key) return -1;
    EVP_PKEY    *pkey  = (EVP_PKEY *)s->signing_key;
    EVP_MD_CTX  *mdctx = EVP_MD_CTX_new();
    size_t       siglen = SIG_BYTES;
    int          ret;

    ret = EVP_DigestSignInit(mdctx, NULL, NULL, NULL, pkey);
    if (ret != 1) { EVP_MD_CTX_free(mdctx); return -1; }
    ret = EVP_DigestSign(mdctx, sig, &siglen, msg, len);
    EVP_MD_CTX_free(mdctx);
    return ret == 1 ? 0 : -1;
}

int keymgmt_verify(SeedState *s, const uint8_t *msg, size_t len,
                   const uint8_t sig[SIG_BYTES]) {
    if (!s->signing_key) return -1;
    EVP_PKEY   *pkey  = (EVP_PKEY *)s->signing_key;
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    int         ret;

    ret = EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pkey);
    if (ret != 1) { EVP_MD_CTX_free(mdctx); return -1; }
    ret = EVP_DigestVerify(mdctx, sig, SIG_BYTES, msg, len);
    EVP_MD_CTX_free(mdctx);
    return ret == 1 ? 0 : -1;
}
