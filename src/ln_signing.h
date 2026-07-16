#ifndef LN_SIGNING_H
#define LN_SIGNING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LN_ED25519_PUBLIC_KEY_BYTES 32
#define LN_ED25519_PRIVATE_KEY_BYTES 64
#define LN_ED25519_SIGNATURE_BYTES 64

typedef struct {
    unsigned char public_key[LN_ED25519_PUBLIC_KEY_BYTES];
    unsigned char private_key[LN_ED25519_PRIVATE_KEY_BYTES];
    char key_id[96];
    char fingerprint_sha256[65];
    int loaded;
} LnSigningKeypair;

int ln_signing_init(void);
int ln_signing_generate(LnSigningKeypair *keypair, const char *key_id);
int ln_signing_save_private_key(const LnSigningKeypair *keypair, const char *path);
int ln_signing_load_private_key(LnSigningKeypair *keypair, const char *path, const char *key_id);
int ln_signing_sign_base64(
    const LnSigningKeypair *keypair,
    const unsigned char *message,
    size_t message_len,
    char *signature_b64,
    size_t signature_b64_len
);
int ln_signing_public_key_base64(
    const LnSigningKeypair *keypair,
    char *public_key_b64,
    size_t public_key_b64_len
);

#ifdef __cplusplus
}
#endif

#endif
