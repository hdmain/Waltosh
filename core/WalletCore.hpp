#pragma once

#include "core/Types.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace waltosh::core {

// Qt-free wallet engine.
// Threads:
//   - worker: create/unlock/import/lock/send/address generation
//   - poller: periodic snapshot while wallet is open
//   - BackgroundSync (inside ltcengine): Litecoin P2P
class WalletCore {
public:
    using Listener = std::function<void(Event)>;

    explicit WalletCore(std::string data_dir);
    ~WalletCore();

    WalletCore(const WalletCore&) = delete;
    WalletCore& operator=(const WalletCore&) = delete;

    void set_listener(Listener listener);
    void start();
    void stop();

    void request_create(std::string password);
    void request_import(std::string mnemonic, std::string password);
    void request_unlock(std::string password);
    void request_lock();
    void request_new_address(AddressType type);
    void request_vanity_address(AddressType type, std::string pattern, bool prefix = false);
    void cancel_vanity();
    void request_send(std::string to_address, int64_t amount_sats, int64_t fee_sat_vb);
    void request_hard_refresh();

    [[nodiscard]] Snapshot snapshot() const;

private:
    struct Impl;

    void enqueue(std::function<void()> job);
    void worker_loop();
    void poller_loop();
    void emit_event(Event event);
    void set_busy(bool busy);
    void join_vanity_thread();
    Snapshot build_snapshot() const;

    std::string data_dir_;
    mutable std::mutex listener_mu_;
    Listener listener_;

    mutable std::mutex state_mu_;
    std::shared_ptr<Impl> impl_;
    bool busy_ = false;
    std::atomic<bool> vanity_cancel_{false};
    std::mutex vanity_mu_;
    std::thread vanity_thread_;
    std::atomic<bool> vanity_running_{false};

    std::mutex queue_mu_;
    std::condition_variable queue_cv_;
    std::queue<std::function<void()>> jobs_;
    std::atomic<bool> stop_{true};
    std::thread worker_;
    std::thread poller_;
};

} // namespace waltosh::core
