#include "ltc/crypto/aead.hpp"

#include "ltc/crypto/hash.hpp"
#include "ltc/crypto/random.hpp"
#include "ltc/crypto/secure.hpp"

#include <argon2.h>

#include <cstring>
#include <stdexcept>

namespace ltc {

Bytes argon2id_hash(const std::string& password, const Bytes& salt, uint32_t memory_kib,
                    uint32_t iterations, uint32_t parallelism, size_t out_len) {
  if (salt.size() < 8) throw std::runtime_error("argon2 salt too short");
  if (out_len == 0 || out_len > 128) throw std::runtime_error("bad argon2 out length");
  Bytes out(out_len);
  const int rc = argon2id_hash_raw(iterations, memory_kib, parallelism, password.data(),
                                   password.size(), salt.data(), salt.size(), out.data(), out.size());
  if (rc != ARGON2_OK) throw std::runtime_error(std::string("argon2id failed: ") + argon2_error_message(rc));
  return out;
}

Bytes aead_seal(const Bytes& key64, const Bytes& plaintext) {
  if (key64.size() < 64) throw std::runtime_error("aead key must be 64 bytes");
  Bytes enc_key(key64.begin(), key64.begin() + 32);
  Bytes mac_key(key64.begin() + 32, key64.begin() + 64);
  Bytes iv = random_bytes(16);
  Bytes ct = aes256_cbc_encrypt(enc_key, iv, plaintext);
  Bytes mac_input;
  mac_input.reserve(iv.size() + ct.size());
  mac_input.insert(mac_input.end(), iv.begin(), iv.end());
  mac_input.insert(mac_input.end(), ct.begin(), ct.end());
  Bytes tag = hmac_sha256(mac_key, mac_input);
  Bytes out;
  out.reserve(iv.size() + ct.size() + tag.size());
  out.insert(out.end(), iv.begin(), iv.end());
  out.insert(out.end(), ct.begin(), ct.end());
  out.insert(out.end(), tag.begin(), tag.end());
  secure_wipe(enc_key);
  secure_wipe(mac_key);
  return out;
}

Bytes aead_open(const Bytes& key64, const Bytes& blob) {
  if (key64.size() < 64) throw std::runtime_error("aead key must be 64 bytes");
  if (blob.size() < 16 + 16 + 32) throw std::runtime_error("aead ciphertext too short");
  Bytes enc_key(key64.begin(), key64.begin() + 32);
  Bytes mac_key(key64.begin() + 32, key64.begin() + 64);
  const size_t tag_len = 32;
  const size_t ct_len = blob.size() - 16 - tag_len;
  Bytes iv(blob.begin(), blob.begin() + 16);
  Bytes ct(blob.begin() + 16, blob.begin() + 16 + ct_len);
  Bytes tag(blob.end() - tag_len, blob.end());
  Bytes mac_input;
  mac_input.reserve(iv.size() + ct.size());
  mac_input.insert(mac_input.end(), iv.begin(), iv.end());
  mac_input.insert(mac_input.end(), ct.begin(), ct.end());
  Bytes expect = hmac_sha256(mac_key, mac_input);
  uint8_t diff = 0;
  for (size_t i = 0; i < tag_len; ++i) diff |= uint8_t(tag[i] ^ expect[i]);
  secure_wipe(mac_key);
  secure_wipe(expect);
  if (diff != 0) {
    secure_wipe(enc_key);
    throw std::runtime_error("wallet MAC check failed (wrong password or corrupt file)");
  }
  Bytes pt = aes256_cbc_decrypt(enc_key, iv, ct);
  secure_wipe(enc_key);
  return pt;
}

}  // namespace ltc
