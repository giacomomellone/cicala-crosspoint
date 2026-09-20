#pragma once
#include <openssl/evp.h>

#include <cstddef>
#include <cstdint>

struct mbedtls_sha256_context {
  EVP_MD_CTX* context;
};
inline void mbedtls_sha256_init(mbedtls_sha256_context* ctx) { ctx->context = EVP_MD_CTX_new(); }
inline void mbedtls_sha256_free(mbedtls_sha256_context* ctx) { EVP_MD_CTX_free(ctx->context); }
inline int mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int) {
  return ctx->context && EVP_DigestInit_ex(ctx->context, EVP_sha256(), nullptr) == 1 ? 0 : -1;
}
inline int mbedtls_sha256_update(mbedtls_sha256_context* ctx, const uint8_t* bytes, size_t size) {
  return EVP_DigestUpdate(ctx->context, bytes, size) == 1 ? 0 : -1;
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* ctx, uint8_t* digest) {
  return EVP_DigestFinal_ex(ctx->context, digest, nullptr) == 1 ? 0 : -1;
}
inline int mbedtls_sha256(const uint8_t* bytes, size_t size, uint8_t* digest, int) {
  return EVP_Digest(bytes, size, digest, nullptr, EVP_sha256(), nullptr) == 1 ? 0 : -1;
}
