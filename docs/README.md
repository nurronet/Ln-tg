# LNWS 0.11 Signing Module MVP

Adds `ln_signing.c/.h` using libsodium Ed25519.

## MSYS2 UCRT64 dependency

```bash
pacman -S mingw-w64-ucrt-x86_64-libsodium
```

Add to the executable sources:

```make
src/ln_signing.c \
src/ln_signing.h \
```

Add libsodium flags through pkg-config:

```make
AM_CFLAGS += $(shell pkg-config --cflags libsodium)
LIBS += $(shell pkg-config --libs libsodium)
```

## Security note

The MVP key file is raw binary and must not be used as-is in production. The next client milestone should protect the private key with Windows DPAPI and restrict filesystem permissions. This module establishes key generation, fingerprints, Base64 public-key export, and detached signatures first.
