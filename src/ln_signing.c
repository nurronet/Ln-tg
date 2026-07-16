#include "ln_signing.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>

static void safe_copy(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    snprintf(dst, dst_len, "%s", src ? src : "");
}

static int fingerprint_public_key(
    const unsigned char public_key[LN_ED25519_PUBLIC_KEY_BYTES],
    char out_hex[65]
) {
    unsigned char digest[crypto_hash_sha256_BYTES];
    size_t i;
    if (crypto_hash_sha256(digest, public_key, LN_ED25519_PUBLIC_KEY_BYTES) != 0)
        return -1;
    for (i = 0; i < sizeof(digest); i++)
        snprintf(out_hex + (i * 2), 3, "%02x", digest[i]);
    out_hex[64] = '\0';
    sodium_memzero(digest, sizeof(digest));
    return 0;
}

int ln_signing_init(void) {
    return sodium_init() < 0 ? -1 : 0;
}

int ln_signing_generate(LnSigningKeypair *keypair, const char *key_id) {
    if (!keypair || !key_id || !*key_id) return -1;
    memset(keypair, 0, sizeof(*keypair));
    if (crypto_sign_keypair(keypair->public_key, keypair->private_key) != 0)
        return -2;
    safe_copy(keypair->key_id, sizeof(keypair->key_id), key_id);
    if (fingerprint_public_key(keypair->public_key, keypair->fingerprint_sha256) != 0)
        return -3;
    keypair->loaded = 1;
    return 0;
}

int ln_signing_save_private_key(const LnSigningKeypair *keypair, const char *path) {
    FILE *f;
    if (!keypair || !keypair->loaded || !path) return -1;
    f = fopen(path, "wb");
    if (!f) return -2;
    if (fwrite(keypair->private_key, 1, sizeof(keypair->private_key), f) != sizeof(keypair->private_key)) {
        fclose(f);
        return -3;
    }
    if (fwrite(keypair->public_key, 1, sizeof(keypair->public_key), f) != sizeof(keypair->public_key)) {
        fclose(f);
        return -4;
    }
    fclose(f);
    return 0;
}

int ln_signing_load_private_key(
    LnSigningKeypair *keypair,
    const char *path,
    const char *key_id
) {
    FILE *f;
    if (!keypair || !path || !key_id) return -1;
    memset(keypair, 0, sizeof(*keypair));
    f = fopen(path, "rb");
    if (!f) return -2;
    if (fread(keypair->private_key, 1, sizeof(keypair->private_key), f) != sizeof(keypair->private_key)) {
        fclose(f);
        return -3;
    }
    if (fread(keypair->public_key, 1, sizeof(keypair->public_key), f) != sizeof(keypair->public_key)) {
        fclose(f);
        return -4;
    }
    fclose(f);
    safe_copy(keypair->key_id, sizeof(keypair->key_id), key_id);
    if (fingerprint_public_key(keypair->public_key, keypair->fingerprint_sha256) != 0)
        return -5;
    keypair->loaded = 1;
    return 0;
}

int ln_signing_sign_base64(
    const LnSigningKeypair *keypair,
    const unsigned char *message,
    size_t message_len,
    char *signature_b64,
    size_t signature_b64_len
) {
    unsigned char signature[LN_ED25519_SIGNATURE_BYTES];
    unsigned long long signature_len = 0;
    size_t required;
    if (!keypair || !keypair->loaded || !message || !signature_b64) return -1;
    required = sodium_base64_ENCODED_LEN(
        LN_ED25519_SIGNATURE_BYTES,
        sodium_base64_VARIANT_ORIGINAL
    );
    if (signature_b64_len < required) return -2;
    if (crypto_sign_detached(
            signature,
            &signature_len,
            message,
            (unsigned long long)message_len,
            keypair->private_key
        ) != 0)
        return -3;
    sodium_bin2base64(
        signature_b64,
        signature_b64_len,
        signature,
        (size_t)signature_len,
        sodium_base64_VARIANT_ORIGINAL
    );
    sodium_memzero(signature, sizeof(signature));
    return 0;
}

int ln_signing_public_key_base64(
    const LnSigningKeypair *keypair,
    char *public_key_b64,
    size_t public_key_b64_len
) {
    size_t required;
    if (!keypair || !keypair->loaded || !public_key_b64) return -1;
    required = sodium_base64_ENCODED_LEN(
        LN_ED25519_PUBLIC_KEY_BYTES,
        sodium_base64_VARIANT_ORIGINAL
    );
    if (public_key_b64_len < required) return -2;
    sodium_bin2base64(
        public_key_b64,
        public_key_b64_len,
        keypair->public_key,
        LN_ED25519_PUBLIC_KEY_BYTES,
        sodium_base64_VARIANT_ORIGINAL
    );
    return 0;
}
