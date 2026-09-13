#pragma once
#include "ltc/net/serialize.hpp"
#include "ltc/util/bytes.hpp"

#include <cstdint>

namespace ltc {

// Litecoin PoW: scrypt(header, header, N=1024, r=1, p=1, dkLen=32).
Hash256 scrypt_pow_hash(const uint8_t* header80);
Hash256 scrypt_pow_hash(const net::BlockHeader& header);

// Bitcoin/Litecoin compact target (nBits) → 32-byte big-endian target.
bool compact_to_target(uint32_t bits, uint8_t out32[32]);
// pow_hash (internal byte order like Hash256) meets compact target?
bool hash_meets_compact_target(const Hash256& pow_hash, uint32_t bits);

// Full PoW check for a header (scrypt + nBits). Max target = Litecoin powLimit.
bool check_header_pow(const net::BlockHeader& header);

}  // namespace ltc
