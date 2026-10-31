#pragma once
#include <cstddef>
#include <cstdint>

// Streaming SHA-256 over the bytes of an OTA image while they are written to
// flash, so a 1.9 MB image never has to sit in RAM (rule 14).
//
// This is a LOGGING aid, not a security control. No install path verifies an
// image: signing (and the compiled-in public key that went with it) was removed
// from every update path by the maintainer on 2026-10-30. The digest is still
// worth having, because it is how a person confirms what the unit actually
// wrote matches the file they sent - compare the install line
//
//     OTA web: image accepted (1757296 bytes, sha256 3f2a1b...)
//
// against `sha256sum firmware.bin`.
//
// There is exactly one context and it is static, so two installs must never
// stream an image at the same time; the upload handler checks
// otaFlashSessionOpen / otaSessionPoisoned before opening a slot, which is what
// keeps that true.
//
// Begin() opens the context and returns void; Open() lets a caller ask whether
// the digest will actually be available, so a path that wants a digest can say
// so instead of assuming one.

void otaImageHashBegin();
bool otaImageHashUpdate(const uint8_t *data, size_t len);
bool otaImageHashFinish(uint8_t digest[32]);
bool otaImageHashOpen();
