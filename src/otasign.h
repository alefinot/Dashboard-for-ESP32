#pragma once
#include <cstddef>
#include <cstdint>

// Signed-firmware verification for the cloud OTA pull (SECURITY plan 2).
//
// Threat model: the pull happens over a network the attacker may sit on (the
// same Wi-Fi, the LAN, a compromised DNS/CDN path, a stolen release asset).
// Without verification, anything served at the OTA URL gets flashed and runs
// with full access to the config API, NVS and both radios. Signature
// verification makes the compiled-in public key the root of trust: only an
// image signed by the maintainer's private key is ever marked bootable.
//
// Scheme (implemented host-side in scripts/ota_sign.py, must match byte for
// byte):
//     payload   = sha256(firmware.bin) as 32 RAW bytes + 0x0A + version string
//     signature = ECDSA P-256 over sha256(payload), DER encoded
//
// The version is inside the signature, so a manifest cannot keep a valid
// signed image and relabel it (or replay an old release under a new version).
//
// ECDSA P-256 rather than Ed25519: the mbedTLS build in Arduino core 3.3.12
// has no Ed25519 at all (checked against libmbedcrypto.a - zero ed25519
// symbols), and enabling it would mean rebuilding the framework.
//
// This is signature verification, not secure boot: the eFuses stay unburned
// on purpose (reversibility, and anyone can still flash their own build over
// USB or the Web UI upload path). It closes the remote-injection hole, which
// is the one reachable over the network.

// A P-256 DER ECDSA signature is 70-72 bytes; 128 leaves generous headroom
// while keeping the fetch bounded (no unbounded reads into RAM).
const size_t OTA_SIG_MAX_LEN = 128;

// False when no public key is compiled in (include/ota_pubkey.h was generated
// with an empty key). Callers must treat that as "refuse the update", never
// as "no signature needed".
bool otaSigningAvailable();

// Streaming SHA-256 over the image bytes as they are written to flash, so a
// 1.9 MB image never has to sit in RAM (rule 14: no big heap allocations).
void otaImageHashBegin();
bool otaImageHashUpdate(const uint8_t *data, size_t len);
bool otaImageHashFinish(uint8_t digest[32]);

// Verifies sigDer against digest + version. Returns true only on a valid
// signature; mbedtlsErr (when given) receives the mbedTLS return code so the
// caller can log why it failed.
bool otaVerifyImage(const uint8_t digest[32], const char *version,
                    const uint8_t *sigDer, size_t sigLen, int *mbedtlsErr);
