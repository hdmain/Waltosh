#include "ltc/wallet/wallet.hpp"

#include "ltc/crypto/aead.hpp"
#include "ltc/crypto/hash.hpp"
#include "ltc/crypto/random.hpp"
#include "ltc/crypto/secure.hpp"
#include "ltc/params.hpp"
#include "ltc/util/fs.hpp"
#include "ltc/wallet/bip39.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ltc {
namespace {

int type_index(WalletAddressType t) {
  switch (t) {
    case WalletAddressType::Legacy:
      return 0;
    case WalletAddressType::Nested:
      return 1;
    case WalletAddressType::Native:
      return 2;
    case WalletAddressType::Taproot:
      return 3;
  }
  return 2;
}

std::string script_key(const Bytes& spk) { return to_hex(spk); }

[[nodiscard]] std::string address_from_script(const Bytes& spk) {
  if (spk.size() == 25 && spk[0] == 0x76 && spk[1] == 0xa9 && spk[2] == 0x14 && spk[23] == 0x88
      && spk[24] == 0xac) {
    Hash160 h{};
    std::memcpy(h.data(), spk.data() + 3, 20);
    return address_p2pkh(h);
  }
  if (spk.size() == 23 && spk[0] == 0xa9 && spk[1] == 0x14 && spk[22] == 0x87) {
    Hash160 h{};
    std::memcpy(h.data(), spk.data() + 2, 20);
    return address_p2sh(h);
  }
  if (spk.size() == 22 && spk[0] == 0x00 && spk[1] == 0x14) {
    Hash160 h{};
    std::memcpy(h.data(), spk.data() + 2, 20);
    return address_p2wpkh(h);
  }
  if (spk.size() == 34 && spk[0] == 0x51 && spk[1] == 0x20) {
    Bytes xonly(spk.begin() + 2, spk.end());
    return address_p2tr(xonly);
  }
  if (spk.size() == 34 && spk[0] == 0x00 && spk[1] == 0x20) {
    Hash256 h{};
    std::memcpy(h.data(), spk.data() + 2, 32);
    return address_p2wsh(h);
  }
  return {};
}

Bytes p2sh_p2wpkh_redeem(const Hash160& h160) {
  Bytes r;
  r.push_back(0x00);
  r.push_back(0x14);
  r.insert(r.end(), h160.begin(), h160.end());
  return r;
}

}  // namespace

const char* wallet_address_type_name(WalletAddressType t) {
  switch (t) {
    case WalletAddressType::Legacy:
      return "legacy";
    case WalletAddressType::Nested:
      return "nested";
    case WalletAddressType::Native:
      return "native";
    case WalletAddressType::Taproot:
      return "taproot";
  }
  return "native";
}

WalletAddressType wallet_address_type_from_name(const std::string& name) {
  if (name == "legacy") return WalletAddressType::Legacy;
  if (name == "nested") return WalletAddressType::Nested;
  if (name == "taproot") return WalletAddressType::Taproot;
  return WalletAddressType::Native;
}

std::string Wallet::wallet_path() const { return fs::join(data_dir_, "wallet.dat"); }
std::string Wallet::utxo_path() const { return fs::join(data_dir_, "utxos.dat"); }
std::string Wallet::tx_history_path() const { return fs::join(data_dir_, "txhistory.dat"); }
std::string Wallet::meta_path() const { return fs::join(data_dir_, "wallet.meta"); }

void Wallet::save_meta() const {
  std::ostringstream oss;
  oss << "META1\n";
  oss << "gap=" << gap_limit_ << "\n";
  oss << "filter_height=" << filter_height_ << "\n";
  for (int i = 0; i < 4; ++i) {
    oss << "used_recv_" << i << "=" << used_recv_[i] << "\n";
    oss << "used_change_" << i << "=" << used_change_[i] << "\n";
    oss << "next_recv_" << i << "=" << next_recv_[i] << "\n";
    oss << "next_change_" << i << "=" << next_change_[i] << "\n";
    oss << "issued_recv_" << i << "=";
    for (size_t j = 0; j < issued_recv_[i].size(); ++j) {
      if (j) oss << ',';
      oss << issued_recv_[i][j];
    }
    oss << "\n";
  }
  fs::write_file(meta_path(), oss.str());
}

void Wallet::load_meta() {
  if (!fs::file_exists(meta_path())) return;
  std::string text = fs::read_text(meta_path());
  std::istringstream iss(text);
  std::string line;
  if (!std::getline(iss, line) || line != "META1") return;
  bool any_issued = false;
  while (std::getline(iss, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    if (key == "gap")
      gap_limit_ = static_cast<uint32_t>(std::stoul(val));
    else if (key == "filter_height")
      filter_height_ = static_cast<uint32_t>(std::stoul(val));
    else if (key.rfind("used_recv_", 0) == 0)
      used_recv_[std::stoi(key.substr(10))] = static_cast<uint32_t>(std::stoul(val));
    else if (key.rfind("used_change_", 0) == 0)
      used_change_[std::stoi(key.substr(12))] = static_cast<uint32_t>(std::stoul(val));
    else if (key.rfind("next_recv_", 0) == 0)
      next_recv_[std::stoi(key.substr(10))] = static_cast<uint32_t>(std::stoul(val));
    else if (key.rfind("next_change_", 0) == 0)
      next_change_[std::stoi(key.substr(12))] = static_cast<uint32_t>(std::stoul(val));
    else if (key.rfind("issued_recv_", 0) == 0) {
      const int ti = std::stoi(key.substr(12));
      if (ti < 0 || ti > 3) continue;
      issued_recv_[ti].clear();
      std::istringstream vs(val);
      std::string part;
      while (std::getline(vs, part, ',')) {
        if (part.empty()) continue;
        issued_recv_[ti].push_back(static_cast<uint32_t>(std::stoul(part)));
        any_issued = true;
      }
    }
  }
  if (!any_issued) migrate_issued_recv_if_needed();
}

void Wallet::migrate_issued_recv_if_needed() {
  for (int ti = 0; ti < 4; ++ti) {
    if (!issued_recv_[ti].empty()) continue;
    for (uint32_t i = 0; i < used_recv_[ti]; ++i) issued_recv_[ti].push_back(i);
  }
}

void Wallet::mark_receive_issued(WalletAddressType type, uint32_t index) {
  const int ti = type_index(type);
  for (uint32_t existing : issued_recv_[ti]) {
    if (existing == index) return;
  }
  issued_recv_[ti].push_back(index);
}

void Wallet::set_filter_height(uint32_t h) {
  if (h <= filter_height_) return;
  filter_height_ = h;
  save_meta();
}

void Wallet::rewind_filter_height(uint32_t h) {
  if (h >= filter_height_) return;
  filter_height_ = h;
  save_meta();
}

ExtKey Wallet::account_root(WalletAddressType type) const {
  std::string path;
  switch (type) {
    case WalletAddressType::Legacy:
      path = "m/44'/2'/0'";
      break;
    case WalletAddressType::Nested:
      path = "m/49'/2'/0'";
      break;
    case WalletAddressType::Native:
      path = "m/84'/2'/0'";
      break;
    case WalletAddressType::Taproot:
      path = "m/86'/2'/0'";
      break;
  }
  return derive_path(master_, path);
}

WatchedAddress Wallet::derive_one(WalletAddressType type, bool change, uint32_t index) {
  ExtKey acct = account_root(type);
  ExtKey chain = derive_child(acct, change ? 1u : 0u);
  ExtKey child = derive_child(chain, index);
  if (!child.is_private || child.key.size() != 32)
    throw std::runtime_error("expected private key at derivation path");

  WatchedAddress wa;
  wa.type = type;
  wa.change = change;
  wa.index = index;
  wa.privkey = child.key;
  wa.pubkey = priv_to_pub(child.key, true);

  Hash160 h160 = hash160(wa.pubkey);
  switch (type) {
    case WalletAddressType::Legacy:
      wa.script_pubkey = script_p2pkh(h160);
      wa.address = address_p2pkh(h160);
      break;
    case WalletAddressType::Nested: {
      wa.redeem_script = p2sh_p2wpkh_redeem(h160);
      Hash160 sh = hash160(wa.redeem_script);
      wa.script_pubkey = script_p2sh(sh);
      wa.address = address_p2sh(sh);
      break;
    }
    case WalletAddressType::Native:
      wa.script_pubkey = script_p2wpkh(h160);
      wa.address = address_p2wpkh(h160);
      break;
    case WalletAddressType::Taproot: {
      // BIP86: address commits to tweaked output key, not the internal key.
      Bytes output = taproot_output_xonly(child.key);
      wa.pubkey = output;
      wa.script_pubkey = script_p2tr(output);
      wa.address = address_p2tr(output);
      break;
    }
  }
  return wa;
}

void Wallet::derive_gap(WalletAddressType type, bool change) {
  int ti = type_index(type);
  uint32_t& next = change ? next_change_[ti] : next_recv_[ti];
  uint32_t used = change ? used_change_[ti] : used_recv_[ti];
  uint32_t target = used + gap_limit_;
  while (next < target) {
    WatchedAddress wa = derive_one(type, change, next);
    script_index_[script_key(wa.script_pubkey)] = addresses_.size();
    addresses_.push_back(std::move(wa));
    ++next;
  }
}

void Wallet::ensure_look_ahead() {
  for (int t = 0; t < 4; ++t) {
    auto type = static_cast<WalletAddressType>(t);
    derive_gap(type, false);
    derive_gap(type, true);
  }
}

void Wallet::init_from_seed(const Bytes& seed) {
  seed_ = seed;
  master_ = master_from_seed(seed_);
  addresses_.clear();
  script_index_.clear();
  for (int i = 0; i < 4; ++i) {
    next_recv_[i] = next_change_[i] = 0;
    used_recv_[i] = used_change_[i] = 0;
    issued_recv_[i].clear();
  }
  ensure_look_ahead();
}

Wallet Wallet::create_new(const std::string& data_dir, const std::string& password, int strength_bits,
                          const std::string& bip39_passphrase) {
  Wallet w;
  w.data_dir_ = data_dir;
  fs::ensure_dir(data_dir);
  w.mnemonic_ = generate_mnemonic(strength_bits);
  Bytes seed = mnemonic_to_seed(w.mnemonic_, bip39_passphrase);
  w.init_from_seed(seed);
  w.save(password);
  return w;
}

Wallet Wallet::import_mnemonic(const std::string& data_dir, const std::string& mnemonic,
                               const std::string& password, const std::string& bip39_passphrase) {
  if (!mnemonic_is_valid(mnemonic)) throw std::runtime_error("invalid mnemonic");
  Wallet w;
  w.data_dir_ = data_dir;
  fs::ensure_dir(data_dir);
  Bytes seed = mnemonic_to_seed(mnemonic, bip39_passphrase);
  w.init_from_seed(seed);
  // Importer already has the words; do not keep a recoverable copy in the wallet.
  w.save(password);
  return w;
}

std::string Wallet::take_mnemonic() {
  std::string out = mnemonic_;
  std::fill(mnemonic_.begin(), mnemonic_.end(), '\0');
  mnemonic_.clear();
  return out;
}

Bytes Wallet::encrypt_seed(const std::string& password, Bytes& salt_out, Bytes& iv_out) const {
  // Legacy helper kept for LTCWALLET1 migration reads only.
  salt_out = random_bytes(16);
  iv_out = random_bytes(16);
  Bytes dk = pbkdf2_hmac_sha512(password, std::string(salt_out.begin(), salt_out.end()), 100000, 64);
  Bytes key(dk.begin(), dk.begin() + 32);
  Bytes ct = aes256_cbc_encrypt(key, iv_out, seed_);
  secure_wipe(dk);
  secure_wipe(key);
  return ct;
}

Bytes Wallet::decrypt_seed(const std::string& password, const Bytes& salt, const Bytes& iv,
                           const Bytes& ciphertext) {
  Bytes dk = pbkdf2_hmac_sha512(password, std::string(salt.begin(), salt.end()), 100000, 64);
  Bytes key(dk.begin(), dk.begin() + 32);
  Bytes pt = aes256_cbc_decrypt(key, iv, ciphertext);
  secure_wipe(dk);
  secure_wipe(key);
  return pt;
}

namespace {

constexpr uint32_t kArgonMemoryKiB = 65536;  // 64 MiB
constexpr uint32_t kArgonIterations = 3;
constexpr uint32_t kArgonParallelism = 1;

std::string build_wallet_plaintext(const Wallet& /*unused*/, const Bytes& seed, uint32_t gap,
                                   const uint32_t used_recv[4], const uint32_t used_change[4],
                                   const uint32_t next_recv[4], const uint32_t next_change[4]) {
  std::ostringstream oss;
  oss << "seed=" << to_hex(seed) << "\n";
  oss << "gap=" << gap << "\n";
  for (int i = 0; i < 4; ++i) {
    oss << "used_recv_" << i << "=" << used_recv[i] << "\n";
    oss << "used_change_" << i << "=" << used_change[i] << "\n";
    oss << "next_recv_" << i << "=" << next_recv[i] << "\n";
    oss << "next_change_" << i << "=" << next_change[i] << "\n";
  }
  return oss.str();
}

}  // namespace

void Wallet::save(const std::string& password) const {
  const std::string plain =
      build_wallet_plaintext(*this, seed_, gap_limit_, used_recv_, used_change_, next_recv_,
                             next_change_);
  Bytes salt = random_bytes(16);
  Bytes key = argon2id_hash(password, salt, kArgonMemoryKiB, kArgonIterations, kArgonParallelism, 64);
  Bytes blob = aead_seal(key, Bytes(plain.begin(), plain.end()));
  secure_wipe(key);

  std::ostringstream oss;
  oss << "WALTOSHW2\n";
  oss << "kdf=argon2id\n";
  oss << "m=" << kArgonMemoryKiB << "\n";
  oss << "t=" << kArgonIterations << "\n";
  oss << "p=" << kArgonParallelism << "\n";
  oss << "salt=" << to_hex(salt) << "\n";
  oss << "blob=" << to_hex(blob) << "\n";
  fs::write_file(wallet_path(), oss.str());
  secure_wipe(salt);
  secure_wipe(blob);
  save_meta();
  save_utxos();
  save_tx_history();
}

void Wallet::save_utxos() const {
  std::ostringstream oss;
  oss << "UTXO1\n";
  for (const auto& u : utxos_) {
    oss << to_hex(u.outpoint.txid.data(), 32) << " " << u.outpoint.vout << " " << u.value << " "
        << u.height << " " << u.key_index << " " << static_cast<int>(u.address_type) << " "
        << (u.spent ? 1 : 0) << " " << to_hex(u.script_pubkey) << "\n";
  }
  fs::write_file(utxo_path(), oss.str());
}

void Wallet::load_utxos() {
  utxos_.clear();
  if (!fs::file_exists(utxo_path())) return;
  std::string text = fs::read_text(utxo_path());
  std::istringstream iss(text);
  std::string line;
  if (!std::getline(iss, line) || line != "UTXO1") return;
  while (std::getline(iss, line)) {
    if (line.empty()) continue;
    std::istringstream ls(line);
    std::string txid_hex, spk_hex;
    uint32_t vout = 0, height = 0, key_index = 0;
    int64_t value = 0;
    int atype = 0, spent = 0;
    ls >> txid_hex >> vout >> value >> height >> key_index >> atype >> spent >> spk_hex;
    Utxo u;
    Bytes txid = from_hex(txid_hex);
    if (txid.size() != 32) continue;
    std::memcpy(u.outpoint.txid.data(), txid.data(), 32);
    u.outpoint.vout = vout;
    u.value = value;
    u.height = height;
    u.key_index = key_index;
    u.address_type = static_cast<AddressType>(atype);
    u.spent = spent != 0;
    u.script_pubkey = from_hex(spk_hex);
    utxos_.push_back(std::move(u));
  }
}

void Wallet::save_tx_history() const {
  std::ostringstream oss;
  oss << "TXHIST1\n";
  for (const auto& r : tx_history_) {
    oss << r.txid << " " << r.amount_sats << " " << r.height << " " << (r.outgoing ? 1 : 0) << " "
        << (r.address.empty() ? "-" : r.address) << "\n";
  }
  fs::write_file(tx_history_path(), oss.str());
}

void Wallet::load_tx_history() {
  tx_history_.clear();
  if (!fs::file_exists(tx_history_path())) return;
  std::string text = fs::read_text(tx_history_path());
  std::istringstream iss(text);
  std::string line;
  if (!std::getline(iss, line) || line != "TXHIST1") return;
  while (std::getline(iss, line)) {
    if (line.empty()) continue;
    std::istringstream ls(line);
    WalletTxRecord r;
    int outgoing = 0;
    ls >> r.txid >> r.amount_sats >> r.height >> outgoing >> r.address;
    if (r.txid.empty()) continue;
    if (r.address == "-") r.address.clear();
    r.outgoing = outgoing != 0;
    tx_history_.push_back(std::move(r));
  }
}

void Wallet::upsert_tx_history(WalletTxRecord rec) {
  for (auto& e : tx_history_) {
    if (e.txid == rec.txid) {
      if (rec.height > 0) e.height = rec.height;
      if (rec.amount_sats > 0) e.amount_sats = rec.amount_sats;
      if (!rec.address.empty()) e.address = rec.address;
      e.outgoing = rec.outgoing;
      save_tx_history();
      return;
    }
  }
  tx_history_.push_back(std::move(rec));
  save_tx_history();
}

void Wallet::ensure_history_from_utxos() {
  if (!tx_history_.empty() || utxos_.empty()) return;

  struct Agg {
    int64_t amount = 0;
    uint32_t height = 0;
    std::string address;
    bool any_unspent = false;
  };
  std::unordered_map<std::string, Agg> by_txid;
  for (const auto& u : utxos_) {
    const std::string key = to_hex(u.outpoint.txid.data(), u.outpoint.txid.size(), true);
    Agg& agg = by_txid[key];
    agg.amount += u.value;
    agg.height = std::max(agg.height, u.height);
    if (!u.spent) agg.any_unspent = true;
    if (agg.address.empty()) {
      auto it = script_index_.find(script_key(u.script_pubkey));
      if (it != script_index_.end()) agg.address = addresses_[it->second].address;
      else agg.address = address_from_script(u.script_pubkey);
    }
  }
  for (auto& [txid, agg] : by_txid) {
    WalletTxRecord r;
    r.txid = txid;
    r.amount_sats = agg.amount;
    r.height = agg.height;
    r.address = std::move(agg.address);
    r.outgoing = false;
    tx_history_.push_back(std::move(r));
  }
  std::sort(tx_history_.begin(), tx_history_.end(), [](const WalletTxRecord& a, const WalletTxRecord& b) {
    if (a.height != b.height) return a.height > b.height;
    return a.txid > b.txid;
  });
  save_tx_history();
}

void Wallet::record_tx_history(const Transaction& tx, uint32_t height) {
  int64_t spent = 0;
  for (const auto& in : tx.vin) {
    for (const auto& u : utxos_) {
      if (u.outpoint.txid == in.prev.txid && u.outpoint.vout == in.prev.vout) {
        spent += u.value;
        break;
      }
    }
  }

  int64_t received = 0;
  std::string our_addr;
  std::string their_addr;
  for (const auto& o : tx.vout) {
    auto it = script_index_.find(script_key(o.script_pubkey));
    if (it != script_index_.end()) {
      received += o.value;
      const WatchedAddress& wa = addresses_[it->second];
      if (our_addr.empty() || (!wa.change && !our_addr.empty())) {
        if (our_addr.empty() || !wa.change) our_addr = wa.address;
      }
    } else if (their_addr.empty()) {
      their_addr = address_from_script(o.script_pubkey);
    }
  }

  if (spent == 0 && received == 0) return;

  Hash256 tid = tx.txid();
  WalletTxRecord rec;
  rec.txid = to_hex(tid.data(), tid.size(), true);
  rec.height = height;
  if (spent > 0) {
    rec.outgoing = true;
    rec.amount_sats = spent - received;
    if (rec.amount_sats < 0) rec.amount_sats = -rec.amount_sats;
    if (rec.amount_sats == 0) rec.amount_sats = spent;
    rec.address = their_addr;
  } else {
    rec.outgoing = false;
    rec.amount_sats = received;
    rec.address = our_addr;
  }
  upsert_tx_history(std::move(rec));
}

Wallet Wallet::load(const std::string& data_dir, const std::string& password) {
  Wallet w;
  w.data_dir_ = data_dir;
  std::string text = fs::read_text(fs::join(data_dir, "wallet.dat"));
  std::istringstream iss(text);
  std::string magic;
  if (!std::getline(iss, magic)) throw std::runtime_error("bad wallet file");

  bool migrate_to_v2 = false;

  if (magic == "WALTOSHW2") {
    uint32_t m = kArgonMemoryKiB, t = kArgonIterations, p = kArgonParallelism;
    std::string salt_hex, blob_hex;
    while (std::getline(iss, magic)) {
      auto eq = magic.find('=');
      if (eq == std::string::npos) continue;
      std::string key = magic.substr(0, eq);
      std::string val = magic.substr(eq + 1);
      if (key == "m") m = static_cast<uint32_t>(std::stoul(val));
      else if (key == "t") t = static_cast<uint32_t>(std::stoul(val));
      else if (key == "p") p = static_cast<uint32_t>(std::stoul(val));
      else if (key == "salt") salt_hex = val;
      else if (key == "blob") blob_hex = val;
    }
    Bytes salt = from_hex(salt_hex);
    Bytes blob = from_hex(blob_hex);
    Bytes key = argon2id_hash(password, salt, m, t, p, 64);
    Bytes plain_bytes = aead_open(key, blob);
    secure_wipe(key);
    secure_wipe(salt);
    secure_wipe(blob);
    std::string plain(plain_bytes.begin(), plain_bytes.end());
    secure_wipe(plain_bytes);
    std::istringstream piss(plain);
    std::string line;
    std::string seed_hex;
    while (std::getline(piss, line)) {
      auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string k = line.substr(0, eq);
      std::string v = line.substr(eq + 1);
      if (k == "seed") seed_hex = v;
      else if (k == "gap") w.gap_limit_ = static_cast<uint32_t>(std::stoul(v));
      else if (k.rfind("used_recv_", 0) == 0)
        w.used_recv_[std::stoi(k.substr(10))] = static_cast<uint32_t>(std::stoul(v));
      else if (k.rfind("used_change_", 0) == 0)
        w.used_change_[std::stoi(k.substr(12))] = static_cast<uint32_t>(std::stoul(v));
      else if (k.rfind("next_recv_", 0) == 0)
        w.next_recv_[std::stoi(k.substr(10))] = static_cast<uint32_t>(std::stoul(v));
      else if (k.rfind("next_change_", 0) == 0)
        w.next_change_[std::stoi(k.substr(12))] = static_cast<uint32_t>(std::stoul(v));
    }
    secure_wipe(plain);
    w.seed_ = from_hex(seed_hex);
    secure_wipe(seed_hex);
  } else if (magic == "LTCWALLET1") {
    migrate_to_v2 = true;
    std::string salt_hex, iv_hex, seed_enc_hex;
    std::string line;
    while (std::getline(iss, line)) {
      auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string key = line.substr(0, eq);
      std::string val = line.substr(eq + 1);
      if (key == "mnemonic")
        continue;
      else if (key == "salt")
        salt_hex = val;
      else if (key == "iv")
        iv_hex = val;
      else if (key == "seed_enc")
        seed_enc_hex = val;
      else if (key == "gap")
        w.gap_limit_ = static_cast<uint32_t>(std::stoul(val));
      else if (key.rfind("used_recv_", 0) == 0)
        w.used_recv_[std::stoi(key.substr(10))] = static_cast<uint32_t>(std::stoul(val));
      else if (key.rfind("used_change_", 0) == 0)
        w.used_change_[std::stoi(key.substr(12))] = static_cast<uint32_t>(std::stoul(val));
      else if (key.rfind("next_recv_", 0) == 0)
        w.next_recv_[std::stoi(key.substr(10))] = static_cast<uint32_t>(std::stoul(val));
      else if (key.rfind("next_change_", 0) == 0)
        w.next_change_[std::stoi(key.substr(12))] = static_cast<uint32_t>(std::stoul(val));
    }
    Bytes salt = from_hex(salt_hex);
    Bytes iv = from_hex(iv_hex);
    Bytes ct = from_hex(seed_enc_hex);
    w.seed_ = decrypt_seed(password, salt, iv, ct);
    secure_wipe(salt);
    secure_wipe(iv);
    secure_wipe(ct);
  } else {
    throw std::runtime_error("bad wallet file magic");
  }

  w.master_ = master_from_seed(w.seed_);
  w.load_meta();

  w.addresses_.clear();
  w.script_index_.clear();
  for (int t = 0; t < 4; ++t) {
    auto type = static_cast<WalletAddressType>(t);
    std::unordered_set<uint32_t> need;
    for (uint32_t idx : w.issued_recv_[t]) need.insert(idx);
    if (need.empty() && w.used_recv_[t] > 0) {
      for (uint32_t i = 0; i < w.used_recv_[t]; ++i) need.insert(i);
    }
    const uint32_t high = w.used_recv_[t];
    for (uint32_t i = high; i < high + w.gap_limit_; ++i) need.insert(i);

    std::vector<uint32_t> ordered(need.begin(), need.end());
    std::sort(ordered.begin(), ordered.end());
    for (uint32_t idx : ordered) {
      WatchedAddress wa = w.derive_one(type, false, idx);
      w.script_index_[script_key(wa.script_pubkey)] = w.addresses_.size();
      w.addresses_.push_back(std::move(wa));
    }
    w.next_recv_[t] = high + w.gap_limit_;

    w.next_change_[t] = 0;
    const uint32_t change_target = w.used_change_[t] + w.gap_limit_;
    while (w.next_change_[t] < change_target) {
      WatchedAddress wa = w.derive_one(type, true, w.next_change_[t]);
      w.script_index_[script_key(wa.script_pubkey)] = w.addresses_.size();
      w.addresses_.push_back(std::move(wa));
      ++w.next_change_[t];
    }
  }
  w.load_utxos();
  w.load_tx_history();
  w.ensure_history_from_utxos();
  if (migrate_to_v2 || text.find("\nmnemonic=") != std::string::npos) {
    w.save(password);  // upgrade to WALTOSHW2 / strip legacy mnemonic
  }
  return w;
}

std::string Wallet::get_new_address(WalletAddressType type) {
  int ti = type_index(type);
  // Hand out the next unused receive address within the gap window.
  uint32_t idx = used_recv_[ti];
  derive_gap(type, false);
  // Find address with this index.
  for (auto& a : addresses_) {
    if (a.type == type && !a.change && a.index == idx) {
      used_recv_[ti] = idx + 1;
      mark_receive_issued(type, idx);
      ensure_look_ahead();
      save_meta();
      return a.address;
    }
  }
  WatchedAddress wa = derive_one(type, false, idx);
  script_index_[script_key(wa.script_pubkey)] = addresses_.size();
  std::string addr = wa.address;
  addresses_.push_back(std::move(wa));
  if (next_recv_[ti] <= idx) next_recv_[ti] = idx + 1;
  used_recv_[ti] = idx + 1;
  mark_receive_issued(type, idx);
  ensure_look_ahead();
  save_meta();
  return addr;
}

std::string Wallet::claim_receive_index(WalletAddressType type, uint32_t index) {
  const int ti = type_index(type);
  std::string addr;
  for (const auto& a : addresses_) {
    if (a.type == type && !a.change && a.index == index) {
      addr = a.address;
      break;
    }
  }
  if (addr.empty()) {
    WatchedAddress wa = derive_one(type, false, index);
    addr = wa.address;
    script_index_[script_key(wa.script_pubkey)] = addresses_.size();
    addresses_.push_back(std::move(wa));
  }
  if (next_recv_[ti] <= index) next_recv_[ti] = index + 1;
  if (used_recv_[ti] <= index) used_recv_[ti] = index + 1;
  mark_receive_issued(type, index);
  ensure_look_ahead();
  save_meta();
  return addr;
}

unsigned Wallet::vanity_thread_count() {
  unsigned n = std::thread::hardware_concurrency();
  if (n == 0) n = 4;
  // Cap avoids oversubscription on huge machines; vanity is CPU-bound.
  if (n > 32) n = 32;
  return n;
}

uint32_t Wallet::vanity_max_pattern_len(WalletAddressType type, bool prefix) {
  // Substring is easier (many positions). Prefix-after-version is ~alphabet^n.
  // Multi-core desktop, ~5 minute budget.
  const bool bech = (type == WalletAddressType::Native || type == WalletAddressType::Taproot);
  if (prefix) {
    return bech ? 5u : 4u;
  }
  return bech ? 7u : 6u;
}

std::string Wallet::vanity_charset(WalletAddressType type) {
  switch (type) {
    case WalletAddressType::Native:
    case WalletAddressType::Taproot:
      // bech32 charset
      return "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    case WalletAddressType::Legacy:
    case WalletAddressType::Nested:
    default:
      // Base58 without 0,O,I,l
      return "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  }
}

std::string Wallet::normalize_vanity_pattern(WalletAddressType type, const std::string& raw,
                                             bool prefix) {
  std::string out;
  out.reserve(raw.size());
  const bool bech = (type == WalletAddressType::Native || type == WalletAddressType::Taproot);
  const std::string cs = vanity_charset(type);
  std::unordered_set<char> allowed(cs.begin(), cs.end());
  for (unsigned char ch : raw) {
    if (std::isspace(ch)) continue;
    char c = static_cast<char>(ch);
    if (bech) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!bech) {
      // Base58 match is case-insensitive later; keep original for validation against charset
      // by checking both cases if needed.
      const char lo = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      if (allowed.count(c) || allowed.count(lo) || allowed.count(up)) {
        out.push_back(c);
        continue;
      }
      throw std::runtime_error(std::string("Character '") + c +
                               "' cannot appear in this address type");
    }
    if (!allowed.count(c)) {
      throw std::runtime_error(std::string("Character '") + c +
                               "' cannot appear in bech32 addresses");
    }
    out.push_back(c);
  }
  if (out.empty()) throw std::runtime_error("Enter a custom word");

  // Prefix mode places the word after L/M/ltc1q/ltc1p. If the user typed that
  // prefix into the word (e.g. Love on Legacy L…), strip it so the result is
  // Love… instead of LLove….
  if (prefix) {
    const char* fixed = "";
    switch (type) {
      case WalletAddressType::Legacy:
        fixed = "L";
        break;
      case WalletAddressType::Nested:
        fixed = "M";
        break;
      case WalletAddressType::Native:
        fixed = "ltc1q";
        break;
      case WalletAddressType::Taproot:
        fixed = "ltc1p";
        break;
    }
    const size_t flen = std::strlen(fixed);
    if (flen > 0 && out.size() >= flen) {
      bool overlap = true;
      for (size_t i = 0; i < flen; ++i) {
        const char a = bech ? out[i]
                            : static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
        const char b = bech ? fixed[i]
                            : static_cast<char>(std::tolower(static_cast<unsigned char>(fixed[i])));
        if (a != b) {
          overlap = false;
          break;
        }
      }
      if (overlap) {
        out.erase(0, flen);
      }
    }
    if (out.empty()) {
      throw std::runtime_error(
          "Word is only the address prefix - add more characters (e.g. Love → ove after L)");
    }
  }

  const uint32_t max_len = vanity_max_pattern_len(type, prefix);
  if (out.size() > max_len) {
    throw std::runtime_error("Custom word too long for ~5 min search (max " +
                             std::to_string(max_len) + " characters)");
  }
  return out;
}

static std::string to_lower_copy(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Bytes to skip before the user-controlled vanity payload (version / HRP).
static size_t vanity_prefix_skip(WalletAddressType type) {
  switch (type) {
    case WalletAddressType::Legacy:
    case WalletAddressType::Nested:
      return 1;  // L… / M…
    case WalletAddressType::Native:
    case WalletAddressType::Taproot:
      return 5;  // ltc1q / ltc1p
  }
  return 1;
}

uint32_t Wallet::find_vanity_index(
    WalletAddressType type, const std::string& pattern, int timeout_sec,
    const std::function<void(uint64_t tried, int elapsed_sec)>& on_progress,
    std::atomic<bool>* cancel, bool prefix) {
  if (timeout_sec <= 0) timeout_sec = 300;
  const std::string needle = normalize_vanity_pattern(type, pattern, prefix);
  const bool bech = (type == WalletAddressType::Native || type == WalletAddressType::Taproot);
  const std::string needle_cmp = bech ? needle : to_lower_copy(needle);
  const size_t skip = prefix ? vanity_prefix_skip(type) : 0;

  const int ti = type_index(type);
  ExtKey acct = account_root(type);
  ExtKey chain = derive_child(acct, 0);  // receive chain (shared, read-only)

  std::atomic<uint32_t> next_idx{used_recv_[ti]};
  std::atomic<uint64_t> tried{0};
  std::atomic<bool> stop{false};
  std::atomic<bool> found{false};
  std::atomic<uint32_t> found_idx{0};
  std::atomic<bool> timed_out{false};

  const auto t0 = std::chrono::steady_clock::now();
  const unsigned workers = vanity_thread_count();

  auto try_index = [&](uint32_t idx) -> bool {
    ExtKey child;
    try {
      child = derive_child(chain, idx);
    } catch (...) {
      return false;  // rare invalid IL - skip
    }
    if (!child.is_private || child.key.size() != 32) return false;

    Bytes pubkey = priv_to_pub(child.key, true);
    Hash160 h160 = hash160(pubkey);
    std::string address;
    switch (type) {
      case WalletAddressType::Legacy:
        address = address_p2pkh(h160);
        break;
      case WalletAddressType::Nested: {
        Bytes redeem = script_p2wpkh(h160);
        address = address_p2sh(hash160(redeem));
        break;
      }
      case WalletAddressType::Native:
        address = address_p2wpkh(h160);
        break;
      case WalletAddressType::Taproot: {
        Bytes output = taproot_output_xonly(child.key);
        address = address_p2tr(output);
        break;
      }
    }

    const std::string hay = bech ? address : to_lower_copy(address);
    if (prefix) {
      return hay.size() >= skip + needle_cmp.size()
          && hay.compare(skip, needle_cmp.size(), needle_cmp) == 0;
    }
    return hay.find(needle_cmp) != std::string::npos;
  };

  auto worker = [&]() {
    while (!stop.load(std::memory_order_relaxed)) {
      if (cancel && cancel->load(std::memory_order_relaxed)) {
        stop.store(true, std::memory_order_relaxed);
        break;
      }
      const auto now = std::chrono::steady_clock::now();
      const int elapsed =
          static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - t0).count());
      if (elapsed >= timeout_sec) {
        timed_out.store(true, std::memory_order_relaxed);
        stop.store(true, std::memory_order_relaxed);
        break;
      }

      const uint32_t idx = next_idx.fetch_add(1, std::memory_order_relaxed);
      if (try_index(idx)) {
        bool was = false;
        if (found.compare_exchange_strong(was, true, std::memory_order_acq_rel)) {
          found_idx.store(idx, std::memory_order_relaxed);
        } else {
          uint32_t prev = found_idx.load(std::memory_order_relaxed);
          while (idx < prev) {
            if (found_idx.compare_exchange_weak(prev, idx, std::memory_order_relaxed)) break;
          }
        }
        stop.store(true, std::memory_order_relaxed);
        break;
      }
      tried.fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(workers);
  for (unsigned i = 0; i < workers; ++i) pool.emplace_back(worker);

  while (!stop.load(std::memory_order_relaxed)) {
    if (cancel && cancel->load(std::memory_order_relaxed)) {
      stop.store(true, std::memory_order_relaxed);
      break;
    }
    const auto now = std::chrono::steady_clock::now();
    const int elapsed =
        static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - t0).count());
    if (elapsed >= timeout_sec) {
      timed_out.store(true, std::memory_order_relaxed);
      stop.store(true, std::memory_order_relaxed);
      break;
    }
    if (on_progress) on_progress(tried.load(std::memory_order_relaxed), elapsed);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  for (auto& t : pool) {
    if (t.joinable()) t.join();
  }

  if (found.load(std::memory_order_acquire)) {
    if (on_progress) {
      const int elapsed = static_cast<int>(
          std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0)
              .count());
      on_progress(tried.load(std::memory_order_relaxed), elapsed);
    }
    return found_idx.load(std::memory_order_relaxed);
  }
  if (cancel && cancel->load(std::memory_order_relaxed)) {
    throw std::runtime_error("Vanity search cancelled");
  }
  if (timed_out.load(std::memory_order_relaxed)) {
    throw std::runtime_error("No address found within 5 minutes - try a shorter word");
  }
  throw std::runtime_error("Vanity search cancelled");
}

std::string Wallet::find_vanity_address(
    WalletAddressType type, const std::string& pattern, int timeout_sec,
    const std::function<void(uint64_t tried, int elapsed_sec)>& on_progress,
    std::atomic<bool>* cancel, bool prefix) {
  const uint32_t idx = find_vanity_index(type, pattern, timeout_sec, on_progress, cancel, prefix);
  return claim_receive_index(type, idx);
}

std::vector<WatchedAddress> Wallet::all_addresses() const { return addresses_; }

std::vector<WatchedAddress> Wallet::issued_receive_addresses() const {
  std::vector<WatchedAddress> out;
  for (int ti = 0; ti < 4; ++ti) {
    const auto type = static_cast<WalletAddressType>(ti);
    for (uint32_t index : issued_recv_[ti]) {
      for (const auto& a : addresses_) {
        if (a.type == type && !a.change && a.index == index) {
          out.push_back(a);
          break;
        }
      }
    }
  }
  return out;
}

std::vector<Bytes> Wallet::watched_scripts() const {
  std::vector<Bytes> out;
  out.reserve(addresses_.size() * 2);
  for (const auto& a : addresses_) {
    out.push_back(a.script_pubkey);
    if (!a.pubkey.empty()) out.push_back(a.pubkey);
    if (!a.redeem_script.empty()) out.push_back(a.redeem_script);
    // Also watch hash160 of pubkey for BIP37 matching flexibility.
    if (a.pubkey.size() == 33) {
      Hash160 h = hash160(a.pubkey);
      out.emplace_back(h.begin(), h.end());
    }
  }
  return out;
}

int64_t Wallet::balance() const {
  int64_t sum = 0;
  for (const auto& u : utxos_)
    if (!u.spent) sum += u.value;
  return sum;
}

void Wallet::on_tx(const Transaction& tx, uint32_t height) {
  // Capture history before mutating UTXO spent flags.
  record_tx_history(tx, height);

  Hash256 tid = tx.txid();
  // Mark spent inputs.
  for (const auto& in : tx.vin) {
    for (auto& u : utxos_) {
      if (!u.spent && u.outpoint.txid == in.prev.txid && u.outpoint.vout == in.prev.vout) {
        u.spent = true;
      }
    }
  }
  // Collect matching outputs.
  for (uint32_t i = 0; i < tx.vout.size(); ++i) {
    const auto& o = tx.vout[i];
    auto it = script_index_.find(script_key(o.script_pubkey));
    if (it == script_index_.end()) continue;
    const WatchedAddress& wa = addresses_[it->second];
    int ti = type_index(wa.type);
    if (wa.change) {
      if (wa.index + 1 > used_change_[ti]) used_change_[ti] = wa.index + 1;
    } else {
      if (wa.index + 1 > used_recv_[ti]) used_recv_[ti] = wa.index + 1;
    }

    bool exists = false;
    for (auto& u : utxos_) {
      if (u.outpoint.txid == tid && u.outpoint.vout == i) {
        exists = true;
        // Upgrade height when a mempool UTXO is seen in a block.
        if (height > 0 && u.height == 0) u.height = height;
        break;
      }
    }
    if (exists) continue;

    Utxo u;
    u.outpoint.txid = tid;
    u.outpoint.vout = i;
    u.value = o.value;
    u.script_pubkey = o.script_pubkey;
    u.height = height;
    u.key_index = wa.index;
    switch (wa.type) {
      case WalletAddressType::Legacy:
        u.address_type = AddressType::P2PKH;
        break;
      case WalletAddressType::Nested:
        u.address_type = AddressType::P2SH;
        break;
      case WalletAddressType::Native:
        u.address_type = AddressType::P2WPKH;
        break;
      case WalletAddressType::Taproot:
        u.address_type = AddressType::P2TR;
        break;
    }
    u.spent = false;
    utxos_.push_back(std::move(u));
  }
  ensure_look_ahead();
  save_meta();
  save_utxos();
}

void Wallet::sync(net::SpvNode& spv, net::SpvNode::ProgressFn on_progress) {
  ensure_look_ahead();
  net::BloomFilter bloom(256, 0.0001, random_u32(), net::BLOOM_UPDATE_ALL);
  fill_bloom(bloom);
  spv.set_bloom(bloom);
  if (!spv.has_peer()) {
    if (!spv.connect_peers()) throw std::runtime_error("no Litecoin P2P peers available");
  }
  spv.sync_headers(on_progress);
  spv.send_filterload();
  uint32_t from = filter_height_;
  uint32_t tip = spv.tip_height();
  constexpr uint32_t kFirstWindow = 10000;
  if (from == 0 && tip > kFirstWindow) from = tip - kFirstWindow;
  if (from < tip) {
    uint32_t scanned = spv.rescan_filtered(
        from, [this](const Transaction& tx, uint32_t height, const Hash256&) { ingest_tx(tx, height); },
        on_progress);
    set_filter_height(scanned);
  }
  spv.sync_filtered(
      [this](const Transaction& tx, uint32_t height, const Hash256&) { ingest_tx(tx, height); },
      on_progress, 60000);
  if (spv.tip_height() > filter_height_) set_filter_height(spv.tip_height());
}

void Wallet::ingest_tx(const Transaction& tx, uint32_t height) { on_tx(tx, height); }

void Wallet::fill_bloom(net::BloomFilter& bloom) const {
  // const_cast look-ahead not available; scripts already gap-derived on load/create.
  for (const auto& s : watched_scripts()) bloom.insert(s);
  for (const auto& u : utxos_) {
    if (u.spent) continue;
    Bytes op;
    append_bytes(op, u.outpoint.txid.data(), 32);
    append_u32le(op, u.outpoint.vout);
    bloom.insert(op);
  }
  // Keep unconfirmed txs matchable so a later merkleblock can upgrade height.
  for (const auto& r : tx_history_) {
    if (r.height != 0 || r.txid.size() != 64) continue;
    Bytes hx = from_hex(r.txid);
    if (hx.size() != 32) continue;
    std::reverse(hx.begin(), hx.end());  // display hex → internal byte order
    bloom.insert(hx);
  }
}

std::string Wallet::send(net::SpvNode& spv, const std::string& to_address, int64_t amount,
                         int64_t fee_rate_sat_vb) {
  if (fee_rate_sat_vb < 0) fee_rate_sat_vb = params::kDefaultFeeRateSatPerVb;
  ensure_look_ahead();

  // Prefer native segwit change.
  int ti = type_index(WalletAddressType::Native);
  uint32_t cidx = used_change_[ti];
  derive_gap(WalletAddressType::Native, true);
  std::string change_addr;
  for (const auto& a : addresses_) {
    if (a.type == WalletAddressType::Native && a.change && a.index == cidx) {
      change_addr = a.address;
      break;
    }
  }
  if (change_addr.empty()) {
    WatchedAddress wa = derive_one(WalletAddressType::Native, true, cidx);
    change_addr = wa.address;
    script_index_[script_key(wa.script_pubkey)] = addresses_.size();
    addresses_.push_back(std::move(wa));
  }

  std::vector<Utxo> available;
  for (const auto& u : utxos_)
    if (!u.spent) available.push_back(u);

  std::vector<size_t> selected;
  Transaction tx = build_send(available, to_address, amount, change_addr, fee_rate_sat_vb, selected);

  // Map selected UTXOs back and sign.
  std::vector<int64_t> amounts;
  std::vector<Bytes> spks;
  amounts.reserve(tx.vin.size());
  spks.reserve(tx.vin.size());

  for (size_t i = 0; i < tx.vin.size(); ++i) {
    const OutPoint& prev = tx.vin[i].prev;
    const Utxo* u = nullptr;
    for (const auto& cand : utxos_) {
      if (cand.outpoint.txid == prev.txid && cand.outpoint.vout == prev.vout) {
        u = &cand;
        break;
      }
    }
    if (!u) throw std::runtime_error("selected utxo missing");
    amounts.push_back(u->value);
    spks.push_back(u->script_pubkey);

    auto sit = script_index_.find(script_key(u->script_pubkey));
    if (sit == script_index_.end()) throw std::runtime_error("no key for utxo");
    const WatchedAddress& wa = addresses_[sit->second];

    switch (wa.type) {
      case WalletAddressType::Legacy:
        sign_input_p2pkh(tx, i, wa.privkey, wa.pubkey);
        break;
      case WalletAddressType::Nested:
        sign_input_p2sh_p2wpkh(tx, i, wa.privkey, priv_to_pub(wa.privkey, true), u->value);
        break;
      case WalletAddressType::Native:
        sign_input_p2wpkh(tx, i, wa.privkey, wa.pubkey, u->value);
        break;
      case WalletAddressType::Taproot:
        sign_input_p2tr(tx, i, wa.privkey, amounts, spks);
        break;
    }
  }

  // Advance change index if change output present.
  if (tx.vout.size() >= 2) {
    used_change_[ti] = cidx + 1;
    ensure_look_ahead();
    save_meta();
  }

  spv.broadcast_tx(tx);
  on_tx(tx, 0);
  Hash256 id = tx.txid();
  return to_hex(id.data(), id.size(), true);
}

}  // namespace ltc
