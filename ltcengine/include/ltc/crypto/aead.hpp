#pragma once
#include "ltc/util/bytes.hpp"

#include <string>

namespace ltc {

// Argon2id → 64-byte key material (enc 32 + mac 32).
Bytes argon2id_hash(const std::string& password, const Bytes& salt, uint32_t memory_kib,
                    uint32_t iterations, uint32_t parallelism, size_t out_len);

// AES-256-CBC + HMAC-SHA256 (encrypt-then-MAC). Returns iv(16) || ct || tag(32).
Bytes aead_seal(const Bytes& key64, const Bytes& plaintext);
// key64 from argon2; blob = iv || ct || tag. Throws on MAC failure.
Bytes aead_open(const Bytes& key64, const Bytes& blob);

}  // namespace ltc
