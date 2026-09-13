#include "ltc/crypto/pow.hpp"

#include "ltc/crypto/hash.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace ltc {
namespace {

// Minimal scrypt (N=1024, r=1, p=1) for Litecoin header PoW only.
void salsa20_8(uint32_t B[16]) {
  uint32_t x[16];
  std::memcpy(x, B, 64);
  auto R = [](uint32_t a, int b) { return (a << b) | (a >> (32 - b)); };
  for (int i = 0; i < 8; i += 2) {
    x[4] ^= R(x[0] + x[12], 7);
    x[8] ^= R(x[4] + x[0], 9);
    x[12] ^= R(x[8] + x[4], 13);
    x[0] ^= R(x[12] + x[8], 18);
    x[9] ^= R(x[5] + x[1], 7);
    x[13] ^= R(x[9] + x[5], 9);
    x[1] ^= R(x[13] + x[9], 13);
    x[5] ^= R(x[1] + x[13], 18);
    x[14] ^= R(x[10] + x[6], 7);
    x[2] ^= R(x[14] + x[10], 9);
    x[6] ^= R(x[2] + x[14], 13);
    x[10] ^= R(x[6] + x[2], 18);
    x[3] ^= R(x[15] + x[11], 7);
    x[7] ^= R(x[3] + x[15], 9);
    x[11] ^= R(x[7] + x[3], 13);
    x[15] ^= R(x[11] + x[7], 18);
    x[1] ^= R(x[0] + x[3], 7);
    x[2] ^= R(x[1] + x[0], 9);
    x[3] ^= R(x[2] + x[1], 13);
    x[0] ^= R(x[3] + x[2], 18);
    x[6] ^= R(x[5] + x[4], 7);
    x[7] ^= R(x[6] + x[5], 9);
    x[4] ^= R(x[7] + x[6], 13);
    x[5] ^= R(x[4] + x[7], 18);
    x[11] ^= R(x[10] + x[9], 7);
    x[8] ^= R(x[11] + x[10], 9);
    x[9] ^= R(x[8] + x[11], 13);
    x[10] ^= R(x[9] + x[8], 18);
    x[12] ^= R(x[15] + x[14], 7);
    x[13] ^= R(x[12] + x[15], 9);
    x[14] ^= R(x[13] + x[12], 13);
    x[15] ^= R(x[14] + x[13], 18);
  }
  for (int i = 0; i < 16; ++i) B[i] += x[i];
}

void blockmix_salsa8(uint32_t* B, size_t r) {
  // r=1 → 128 bytes = 32 uint32
  uint32_t X[16];
  std::memcpy(X, B + (2 * r - 1) * 16, 64);
  std::vector<uint32_t> Y(32 * r);
  for (size_t i = 0; i < 2 * r; ++i) {
    for (int j = 0; j < 16; ++j) X[j] ^= B[i * 16 + j];
    salsa20_8(X);
    std::memcpy(Y.data() + i * 16, X, 64);
  }
  for (size_t i = 0; i < r; ++i)
    std::memcpy(B + i * 16, Y.data() + (2 * i) * 16, 64);
  for (size_t i = 0; i < r; ++i)
    std::memcpy(B + (i + r) * 16, Y.data() + (2 * i + 1) * 16, 64);
}

uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void wr32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
  p[2] = uint8_t(v >> 16);
  p[3] = uint8_t(v >> 24);
}

// PBKDF2-HMAC-SHA256 for scrypt (need local HMAC-SHA256).
Bytes hmac_sha256_local(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len) {
  uint8_t k[64]{};
  if (key_len > 64) {
    Hash256 h = sha256(key, key_len);
    std::memcpy(k, h.data(), 32);
  } else {
    std::memcpy(k, key, key_len);
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  Bytes inner;
  inner.reserve(64 + data_len);
  inner.insert(inner.end(), ipad, ipad + 64);
  inner.insert(inner.end(), data, data + data_len);
  Hash256 ih = sha256(inner);
  Bytes outer;
  outer.reserve(96);
  outer.insert(outer.end(), opad, opad + 64);
  outer.insert(outer.end(), ih.begin(), ih.end());
  Hash256 oh = sha256(outer);
  return Bytes(oh.begin(), oh.end());
}

Bytes pbkdf2_sha256(const uint8_t* pass, size_t pass_len, const uint8_t* salt, size_t salt_len,
                    int rounds, size_t dk_len) {
  Bytes out(dk_len);
  size_t offset = 0;
  uint32_t block = 1;
  while (offset < dk_len) {
    Bytes asalt(salt, salt + salt_len);
    asalt.push_back(uint8_t(block >> 24));
    asalt.push_back(uint8_t(block >> 16));
    asalt.push_back(uint8_t(block >> 8));
    asalt.push_back(uint8_t(block));
    Bytes u = hmac_sha256_local(pass, pass_len, asalt.data(), asalt.size());
    Bytes t = u;
    for (int i = 1; i < rounds; ++i) {
      u = hmac_sha256_local(pass, pass_len, u.data(), u.size());
      for (size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
    }
    size_t n = std::min(t.size(), dk_len - offset);
    std::memcpy(out.data() + offset, t.data(), n);
    offset += n;
    ++block;
  }
  return out;
}

Hash256 scrypt_1024_1_1_256(const uint8_t* msg, size_t msg_len) {
  constexpr size_t N = 1024;
  constexpr size_t r = 1;
  constexpr size_t p = 1;
  constexpr size_t MFLen = 128 * r;  // 128
  Bytes B = pbkdf2_sha256(msg, msg_len, msg, msg_len, 1, p * MFLen);
  std::vector<uint32_t> X(MFLen / 4);
  for (size_t i = 0; i < X.size(); ++i) X[i] = le32(B.data() + i * 4);

  std::vector<std::vector<uint32_t>> V(N, std::vector<uint32_t>(X.size()));
  for (size_t i = 0; i < N; ++i) {
    V[i] = X;
    blockmix_salsa8(X.data(), r);
  }
  for (size_t i = 0; i < N; ++i) {
    uint32_t j = X[(2 * r - 1) * 16] & (N - 1);
    for (size_t k = 0; k < X.size(); ++k) X[k] ^= V[j][k];
    blockmix_salsa8(X.data(), r);
  }
  for (size_t i = 0; i < X.size(); ++i) wr32(B.data() + i * 4, X[i]);
  Bytes out = pbkdf2_sha256(msg, msg_len, B.data(), B.size(), 1, 32);
  Hash256 h{};
  std::memcpy(h.data(), out.data(), 32);
  return h;
}

}  // namespace

Hash256 scrypt_pow_hash(const uint8_t* header80) {
  return scrypt_1024_1_1_256(header80, 80);
}

Hash256 scrypt_pow_hash(const net::BlockHeader& header) {
  Bytes ser = header.serialize();
  if (ser.size() != 80) throw std::runtime_error("bad header size");
  return scrypt_pow_hash(ser.data());
}

bool compact_to_target(uint32_t bits, uint8_t out32[32]) {
  std::memset(out32, 0, 32);
  uint32_t mantissa = bits & 0x007fffff;
  int exp = int(bits >> 24);
  bool neg = (bits & 0x00800000) != 0;
  if (neg || mantissa == 0) return false;
  // Compact → 256-bit big-endian target (Bitcoin SetCompact semantics).
  if (exp <= 3) {
    uint32_t v = mantissa >> (8 * (3 - exp));
    out32[29] = uint8_t(v >> 16);
    out32[30] = uint8_t(v >> 8);
    out32[31] = uint8_t(v);
  } else {
    int nbytes = exp;
    if (nbytes > 32) return false;
    int start = 32 - nbytes;
    out32[start] = uint8_t(mantissa >> 16);
    if (start + 1 < 32) out32[start + 1] = uint8_t(mantissa >> 8);
    if (start + 2 < 32) out32[start + 2] = uint8_t(mantissa);
  }
  return true;
}

bool hash_meets_compact_target(const Hash256& pow_hash, uint32_t bits) {
  uint8_t target[32];
  if (!compact_to_target(bits, target)) return false;
  // Litecoin powLimit: 00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
  static const uint8_t kPowLimit[32] = {
      0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  // Compare target <= powLimit (big-endian).
  if (std::memcmp(target, kPowLimit, 32) > 0) return false;

  // pow_hash is internal (little-endian) byte order → compare as big-endian numerical.
  uint8_t hash_be[32];
  for (int i = 0; i < 32; ++i) hash_be[i] = pow_hash[31 - i];
  return std::memcmp(hash_be, target, 32) <= 0;
}

bool check_header_pow(const net::BlockHeader& header) {
  if (header.bits == 0 || (header.bits & 0x00800000)) return false;
  Hash256 pow = scrypt_pow_hash(header);
  return hash_meets_compact_target(pow, header.bits);
}

}  // namespace ltc
