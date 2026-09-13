#pragma once
#include <cstdint>
#include <string>

namespace ltc {
namespace params {

inline constexpr uint32_t kMagicMainnet = 0xdbb6c0fb;  // wire bytes: fb c0 b6 db

inline constexpr uint16_t kDefaultPort = 9333;
inline constexpr uint8_t kP2PKHVersion = 48;  // L...
inline constexpr uint8_t kP2SHVersion = 50;   // M...
inline constexpr const char* kBech32HRP = "ltc";
inline constexpr uint32_t kProtocolVersion = 70015;
inline constexpr uint64_t kNodeNetwork = 1;
inline constexpr uint64_t kNodeBloom = 4;
inline constexpr uint64_t kNodeWitness = 8;
// Advertise witness so peers send BIP144 txs/blocks we can deserialize fully.
inline constexpr uint64_t kServices = kNodeNetwork | kNodeWitness;
inline constexpr uint32_t kBip44CoinType = 2;

// Litecoin genesis hash (internal byte order / display reversed hex):
// 12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2
inline const char* kGenesisHashHex =
    "12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2";

inline const char* kDnsSeeds[] = {
    "dnsseed.litecointools.com",
    "seed-a.litecoin.loshan.co.uk",
    "dnsseed.thrasher.io",
    "dnsseed.litecoinpool.org",
    "seed.ltc.xurious.com",
    nullptr,
};

inline constexpr int64_t kDustThreshold = 546;
inline constexpr int64_t kDefaultFeeRateSatPerVb = 10;  // sats/vbyte

struct Checkpoint {
  uint32_t height;
  const char* hash_hex;  // display-order (big-endian) hex like Litecoin Core
};

// Hard checkpoints + Litecoin Core defaultAssumeValid tip anchor.
inline constexpr Checkpoint kCheckpoints[] = {
    {1500, "841a2965955dd288cfa707a755d05a54e45f8bd476835ec9af4402a2b59a2967"},
    {4032, "9ce90e427198fc0ef05e5905ce3503725b80e26afd35a987965fd7e3d9cf0846"},
    {8064, "eb984353fc5190f210651f150c40b8a4bab9eeeff0b729fcb3987da694430d70"},
    {16128, "602edf1859b7f9a6af809f1d9b0e6cb66fdc1d4d9dcd7a4bec03e12a1ccd153d"},
    {23420, "d80fdf9ca81afd0bd2b2a90ac3a9fe547da58f2530ec874e978fce0b5101b507"},
    {50000, "69dc37eb029b68f075a5012dcc0419c127672adb4f3a32882b2b3e71d07a20a6"},
    {80000, "4fcb7c02f676a300503f49c764a89955a8f920b46a8cbecb4867182ecdb2e90a"},
    {120000, "bd9d26924f05f6daa7f0155f32828ec89e8e29cee9e7121b026a7a3552ac6131"},
    {161500, "dbe89880474f4bb4f75c227c77ba1cdc024991123b28b8418dbbf7798471ff43"},
    {179620, "2ad9c65c990ac00426d18e446e0fd7be2ffa69e9a7dcb28358a50b2b78b9f709"},
    {240000, "7140d1c4b4c2157ca217ee7636f24c9c73db39c4590c4e6eab2e3ea1555088aa"},
    {383640, "2b6809f094a9215bafc65eb3f110a35127a34be94b7d0590a096c3f126c6f364"},
    {409004, "487518d663d9f1fa08611d9395ad74d982b667fbdc0e77e9cf39b4f1355908a3"},
    {456000, "bf34f71cc6366cd487930d06be22f897e34ca6a40501ac7d401be32456372004"},
    {638902, "15238656e8ec63d28de29a8c75fcf3a5819afc953dcd9cc45cecc53baec74f38"},
    {721000, "198a7b4de1df9478e2463bd99d75b714eab235a2e63e741641dc8a759a9840e5"},
    // Litecoin Core defaultAssumeValid (skip expensive scrypt below this height).
    {2772730, "80cdb35c080484df5bf384b311fde3c4694d3405765bc0f596e9eb369ff286e5"},
};

inline constexpr uint32_t kLastCheckpointHeight = 2772730;

}  // namespace params
}  // namespace ltc
