#include "otahash.h"
#include <mbedtls/sha256.h>

// Fail at build time instead of discovering mid-install that the framework's
// mbedTLS was compiled without SHA-256.
#if !defined(MBEDTLS_SHA256_C)
#error "The OTA image digest needs mbedTLS SHA-256 (MBEDTLS_SHA256_C)"
#endif

namespace {
// One streaming hash context. Static on purpose: an install task must not
// heap-allocate in the middle of a flash write, and the digest is worth a
// context that both install paths can share.
mbedtls_sha256_context g_sha;
bool g_shaOpen = false;
}  // namespace

void otaImageHashBegin() {
  if (g_shaOpen) {
    mbedtls_sha256_free(&g_sha);
    g_shaOpen = false;
  }
  mbedtls_sha256_init(&g_sha);
  if (mbedtls_sha256_starts(&g_sha, 0 /* SHA-256, not SHA-224 */) == 0)
    g_shaOpen = true;
}

bool otaImageHashUpdate(const uint8_t *data, size_t len) {
  if (!g_shaOpen || !data || !len) return false;
  return mbedtls_sha256_update(&g_sha, data, len) == 0;
}

bool otaImageHashOpen() { return g_shaOpen; }

bool otaImageHashFinish(uint8_t digest[32]) {
  if (!g_shaOpen || !digest) return false;
  int rc = mbedtls_sha256_finish(&g_sha, digest);
  mbedtls_sha256_free(&g_sha);
  g_shaOpen = false;
  return rc == 0;
}
