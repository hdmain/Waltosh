#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace waltosh::core {

enum class AddressType {
    Native,
    Legacy,
    Nested,
    Taproot,
};

struct SyncInfo {
    bool running = false;
    bool connected = false;
    bool rescanning = false;
    bool hard_refresh = false;
    std::string phase;
    std::string connection;
    std::string progress;
    std::string detail;
    std::string last_error;
    std::string peer_agent;
    uint32_t tip_height = 0;
    uint32_t target_height = 0;
    uint32_t rescan_height = 0;
    int peers = 0;
    uint64_t matched_txs = 0;
};

struct TxHistoryEntry {
    std::string txid;       // display-order hex
    std::string address;    // counterparty or wallet address
    int64_t amount_sats = 0;
    uint32_t height = 0;
    bool outgoing = false;
    bool spent = false;
    int outputs = 0;
};

struct Snapshot {
    bool open = false;
    bool exists = false;
    bool busy = false;
    std::string data_dir;
    int64_t balance_sats = 0;
    std::vector<std::string> receive_addresses;
    std::vector<TxHistoryEntry> tx_history;
    SyncInfo sync;
};

enum class EventKind {
    Snapshot,
    BusyChanged,
    Opened,
    Locked,
    Created,           // message = mnemonic
    Imported,
    AddressGenerated,  // message = address
    Sent,              // message = txid
    VanityProgress,    // message = "tried|elapsedSec"
    Error,             // message = error text
};

struct Event {
    EventKind kind = EventKind::Snapshot;
    bool ok = true;
    std::string message;
    Snapshot snapshot;
};

} // namespace waltosh::core
