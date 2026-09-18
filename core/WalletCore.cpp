#include "WalletCore.hpp"

#include "ltc/cli/background_sync.hpp"
#include "ltc/crypto/secure.hpp"
#include "ltc/net/spv.hpp"
#include "ltc/params.hpp"
#include "ltc/util/bytes.hpp"
#include "ltc/util/error_log.hpp"
#include "ltc/util/fs.hpp"
#include "ltc/wallet/wallet.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

namespace waltosh::core {
namespace {

ltc::WalletAddressType to_ltc(AddressType type)
{
    switch (type) {
    case AddressType::Legacy:
        return ltc::WalletAddressType::Legacy;
    case AddressType::Nested:
        return ltc::WalletAddressType::Nested;
    case AddressType::Taproot:
        return ltc::WalletAddressType::Taproot;
    case AddressType::Native:
    default:
        return ltc::WalletAddressType::Native;
    }
}

bool wallet_file_exists(const std::string& data_dir)
{
    return std::filesystem::exists(std::filesystem::path(data_dir) / "wallet.dat");
}

} // namespace

struct WalletCore::Impl {
    std::shared_ptr<ltc::Wallet> wallet;
    std::string password;
    ltc::BackgroundSync sync;
};

WalletCore::WalletCore(std::string data_dir)
    : data_dir_(std::move(data_dir))
    , impl_(std::make_shared<Impl>())
{
}

WalletCore::~WalletCore()
{
    stop();
}

void WalletCore::set_listener(Listener listener)
{
    std::lock_guard<std::mutex> lock(listener_mu_);
    listener_ = std::move(listener);
}

void WalletCore::start()
{
    if (!stop_.exchange(false)) {
        return;
    }
    worker_ = std::thread([this]() { worker_loop(); });
    poller_ = std::thread([this]() { poller_loop(); });
    // Warm P2P peers + headers before the user unlocks (no keys required).
    if (impl_) {
        impl_->sync.start_headers_only(data_dir_);
    }
    emit_event(Event{EventKind::Snapshot, true, {}, build_snapshot()});
}

void WalletCore::stop()
{
    if (stop_.exchange(true)) {
        return;
    }
    vanity_cancel_.store(true);
    queue_cv_.notify_all();

    // Stop P2P first so close isn't blocked on peer I/O.
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        if (impl_) {
            impl_->sync.stop();
        }
    }

    join_vanity_thread();

    if (worker_.joinable()) {
        worker_.join();
    }
    if (poller_.joinable()) {
        poller_.join();
    }

    std::lock_guard<std::mutex> lock(state_mu_);
    if (impl_) {
        impl_->wallet.reset();
        impl_->password.clear();
    }
    busy_ = false;
}

void WalletCore::join_vanity_thread()
{
    std::thread t;
    {
        std::lock_guard<std::mutex> lock(vanity_mu_);
        if (vanity_thread_.joinable()) {
            t = std::move(vanity_thread_);
        }
    }
    if (t.joinable()) {
        t.join();
    }
    vanity_running_.store(false);
}

void WalletCore::request_create(std::string password)
{
    vanity_cancel_.store(true);
    enqueue([this, password = std::move(password)]() mutable {
        set_busy(true);
        Event out{EventKind::Created, false, {}, {}};
        try {
            if (password.empty()) {
                throw std::runtime_error("Password is required.");
            }
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->wallet.reset();
                impl_->password.clear();
            }
            impl_->sync.detach_wallet();
            ltc::fs::ensure_dir(data_dir_);
            ltc::set_error_log_dir(data_dir_);
            auto wallet = std::make_shared<ltc::Wallet>(ltc::Wallet::create_new(data_dir_, password));
            out.message = wallet->take_mnemonic();
            (void)wallet->get_new_address(ltc::WalletAddressType::Nested);
            wallet->save(password);
            ltc::Wallet* raw = nullptr;
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->wallet = std::move(wallet);
                impl_->password = password;
                raw = impl_->wallet.get();
            }
            ltc::secure_wipe(password);
            // Attach to pre-login header sync when possible (avoids reconnect).
            impl_->sync.start(raw, impl_->password);
            out.ok = true;
            out.snapshot = build_snapshot();
            emit_event(Event{EventKind::Opened, true, {}, out.snapshot});
            emit_event(out);
        } catch (const std::exception& e) {
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->wallet.reset();
                ltc::secure_wipe(impl_->password);
            }
            ltc::secure_wipe(password);
            emit_event(Event{EventKind::Error, false, e.what(), build_snapshot()});
        }
        set_busy(false);
        emit_event(Event{EventKind::Snapshot, true, {}, build_snapshot()});
    });
}

void WalletCore::request_import(std::string mnemonic, std::string password)
{
    vanity_cancel_.store(true);
    enqueue([this, mnemonic = std::move(mnemonic), password = std::move(password)]() mutable {
        set_busy(true);
        try {
            if (password.empty() || mnemonic.empty()) {
                throw std::runtime_error("Mnemonic and password are required.");
            }
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->wallet.reset();
                impl_->password.clear();
            }
            impl_->sync.detach_wallet();
            ltc::fs::ensure_dir(data_dir_);
            ltc::set_error_log_dir(data_dir_);
            auto wallet = std::make_shared<ltc::Wallet>(
                ltc::Wallet::import_mnemonic(data_dir_, mnemonic, password));
            ltc::secure_wipe(mnemonic);
            (void)wallet->get_new_address(ltc::WalletAddressType::Nested);
            wallet->save(password);
            ltc::Wallet* raw = nullptr;
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->wallet = std::move(wallet);
                impl_->password = password;
                raw = impl_->wallet.get();
            }
            ltc::secure_wipe(password);
            impl_->sync.start(raw, impl_->password);
            const Snapshot snap = build_snapshot();
            emit_event(Event{EventKind::Opened, true, {}, snap});
            emit_event(Event{EventKind::Imported, true, {}, snap});
        } catch (const std::exception& e) {
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->wallet.reset();
                ltc::secure_wipe(impl_->password);
            }
            ltc::secure_wipe(mnemonic);
            ltc::secure_wipe(password);
            emit_event(Event{EventKind::Error, false, e.what(), build_snapshot()});
        }
        set_busy(false);
        emit_event(Event{EventKind::Snapshot, true, {}, build_snapshot()});
    });
}

void WalletCore::request_unlock(std::string password)
{
    // Abort vanity immediately so Unlock is not stuck behind a 5-minute grind.
    vanity_cancel_.store(true);
    enqueue([this, password = std::move(password)]() mutable {
        set_busy(true);
        try {
            if (password.empty()) {
                throw std::runtime_error("Password is required.");
            }
            // Keep pre-login header sync alive - attach wallet after load.
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->wallet.reset();
                impl_->password.clear();
            }
            impl_->sync.detach_wallet();

            ltc::set_error_log_dir(data_dir_);
            auto wallet = std::make_shared<ltc::Wallet>(ltc::Wallet::load(data_dir_, password));
            ltc::Wallet* raw = nullptr;
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->wallet = std::move(wallet);
                impl_->password = password;
                raw = impl_->wallet.get();
            }
            ltc::secure_wipe(password);
            impl_->sync.start(raw, impl_->password);
            impl_->sync.request_soft_rescan(288);
            emit_event(Event{EventKind::Opened, true, {}, build_snapshot()});
        } catch (const std::exception& e) {
            impl_->sync.detach_wallet();
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->wallet.reset();
                ltc::secure_wipe(impl_->password);
            }
            ltc::secure_wipe(password);
            if (!impl_->sync.running()) {
                impl_->sync.start_headers_only(data_dir_);
            }
            emit_event(Event{EventKind::Error, false, e.what(), build_snapshot()});
        }
        set_busy(false);
        emit_event(Event{EventKind::Snapshot, true, {}, build_snapshot()});
    });
}

void WalletCore::request_lock()
{
    vanity_cancel_.store(true);
    enqueue([this]() {
        set_busy(true);
        {
            std::lock_guard<std::mutex> lock(state_mu_);
            impl_->wallet.reset();
            ltc::secure_wipe(impl_->password);
        }
        // Keep peers/headers warm while locked (no secrets held).
        impl_->sync.detach_wallet();
        if (!impl_->sync.running()) {
            impl_->sync.start_headers_only(data_dir_);
        }
        set_busy(false);
        const Snapshot snap = build_snapshot();
        emit_event(Event{EventKind::Locked, true, {}, snap});
        emit_event(Event{EventKind::Snapshot, true, {}, snap});
    });
}

void WalletCore::request_new_address(AddressType type)
{
    enqueue([this, type]() {
        set_busy(true);
        try {
            std::string addr;
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                if (!impl_->wallet) {
                    throw std::runtime_error("Wallet is locked.");
                }
                std::lock_guard<std::mutex> wlock(impl_->sync.wallet_mutex());
                addr = impl_->wallet->get_new_address(to_ltc(type));
                impl_->wallet->save(impl_->password);
                impl_->sync.request_soft_rescan(288);
            }
            emit_event(Event{EventKind::AddressGenerated, true, addr, build_snapshot()});
        } catch (const std::exception& e) {
            emit_event(Event{EventKind::Error, false, e.what(), build_snapshot()});
        }
        set_busy(false);
        emit_event(Event{EventKind::Snapshot, true, {}, build_snapshot()});
    });
}

void WalletCore::cancel_vanity()
{
    vanity_cancel_.store(true);
}

void WalletCore::request_vanity_address(AddressType type, std::string pattern, bool prefix)
{
    // Launch from the worker (never join on the UI thread). The grind itself runs on
    // vanity_thread_ so Unlock/Lock jobs are not stuck behind the search.
    enqueue([this, type, pattern = std::move(pattern), prefix]() mutable {
        vanity_cancel_.store(true);
        join_vanity_thread();
        vanity_cancel_.store(false);
        if (vanity_running_.exchange(true)) {
            return;
        }

        std::lock_guard<std::mutex> vlock(vanity_mu_);
        vanity_thread_ = std::thread([this, type, pattern = std::move(pattern), prefix]() mutable {
            set_busy(true);
            try {
                std::string addr;
                std::shared_ptr<ltc::Wallet> wallet;
                std::string password;
                {
                    std::lock_guard<std::mutex> lock(state_mu_);
                    if (!impl_->wallet) {
                        throw std::runtime_error("Wallet is locked.");
                    }
                    wallet = impl_->wallet;
                    password = impl_->password;
                    impl_->sync.pause();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                const uint32_t vanity_idx = wallet->find_vanity_index(
                    to_ltc(type),
                    pattern,
                    300,
                    [this](uint64_t tried, int elapsed) {
                        emit_event(Event{EventKind::VanityProgress, true,
                                         std::to_string(tried) + "|" + std::to_string(elapsed),
                                         Snapshot{}});
                    },
                    &vanity_cancel_,
                    prefix);

                {
                    std::lock_guard<std::mutex> lock(state_mu_);
                    if (!impl_->wallet || impl_->wallet.get() != wallet.get()) {
                        throw std::runtime_error("Wallet changed during vanity search");
                    }
                    std::lock_guard<std::mutex> wlock(impl_->sync.wallet_mutex());
                    addr = wallet->claim_receive_index(to_ltc(type), vanity_idx);
                    wallet->save(password);
                    impl_->sync.resume();
                    impl_->sync.request_soft_rescan(288);
                    impl_->sync.notify_wallet_changed();
                }
                emit_event(Event{EventKind::AddressGenerated, true, addr, build_snapshot()});
            } catch (const std::exception& e) {
                {
                    std::lock_guard<std::mutex> lock(state_mu_);
                    if (impl_) {
                        impl_->sync.resume();
                    }
                }
                emit_event(Event{EventKind::Error, false, e.what(), build_snapshot()});
            }
            vanity_cancel_.store(false);
            vanity_running_.store(false);
            set_busy(false);
            emit_event(Event{EventKind::Snapshot, true, {}, build_snapshot()});
        });
    });
}

void WalletCore::request_send(std::string to_address, int64_t amount_sats, int64_t fee_sat_vb)
{
    enqueue([this, to = std::move(to_address), amount_sats, fee_sat_vb]() mutable {
        set_busy(true);
        try {
            if (to.empty() || amount_sats <= 0) {
                throw std::runtime_error("Enter a valid destination and amount.");
            }
            std::string txid;
            {
                std::unique_lock<std::mutex> lock(state_mu_);
                if (!impl_->wallet) {
                    throw std::runtime_error("Wallet is locked.");
                }
                impl_->sync.pause();
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                lock.lock();
                std::lock_guard<std::mutex> wlock(impl_->sync.wallet_mutex());
                ltc::net::SpvNode spv(data_dir_);
                if (!spv.connect_peers(1, 2, false)) {
                    throw std::runtime_error("no peers for broadcast");
                }
                const int64_t fee =
                    fee_sat_vb < 0 ? ltc::params::kDefaultFeeRateSatPerVb : fee_sat_vb;
                txid = impl_->wallet->send(spv, to, amount_sats, fee);
                impl_->wallet->save(impl_->password);
                impl_->sync.resume();
                impl_->sync.request_bloom_refresh();
                // Wake the UI poller immediately (TUI re-read wallet balance after send).
                impl_->sync.notify_wallet_changed();
            }
            emit_event(Event{EventKind::Sent, true, txid, build_snapshot()});
        } catch (const std::exception& e) {
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                impl_->sync.resume();
            }
            emit_event(Event{EventKind::Error, false, e.what(), build_snapshot()});
        }
        set_busy(false);
        emit_event(Event{EventKind::Snapshot, true, {}, build_snapshot()});
    });
}

void WalletCore::request_hard_refresh()
{
    enqueue([this]() {
        try {
            std::lock_guard<std::mutex> lock(state_mu_);
            if (!impl_->wallet) {
                throw std::runtime_error("Wallet is locked.");
            }
            std::lock_guard<std::mutex> wlock(impl_->sync.wallet_mutex());
            const uint32_t tip = std::max(impl_->sync.status().tip_height, impl_->wallet->filter_height());
            constexpr uint32_t kLookback = 2016;
            const uint32_t from = tip > kLookback ? tip - kLookback : 0;
            impl_->wallet->rewind_filter_height(from);
            impl_->sync.request_bloom_refresh();
            impl_->sync.request_hard_refresh();
            emit_event(Event{EventKind::Snapshot, true, {}, build_snapshot()});
        } catch (const std::exception& e) {
            emit_event(Event{EventKind::Error, false, e.what(), build_snapshot()});
        }
    });
}

Snapshot WalletCore::snapshot() const
{
    return build_snapshot();
}

void WalletCore::enqueue(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(queue_mu_);
        jobs_.push(std::move(job));
    }
    queue_cv_.notify_one();
}

void WalletCore::worker_loop()
{
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(queue_mu_);
            queue_cv_.wait(lock, [this]() { return stop_.load() || !jobs_.empty(); });
            if (stop_.load() && jobs_.empty()) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        job();
    }
}

void WalletCore::poller_loop()
{
    while (!stop_.load()) {
        bool wake = false;
        {
            std::lock_guard<std::mutex> lock(state_mu_);
            if (impl_) {
                wake = impl_->sync.consume_wallet_changed();
            }
        }
        Snapshot snap = build_snapshot();
        // Emit while locked too so the gate screen can show header warm-up progress.
        if (snap.open || snap.sync.running) {
            emit_event(Event{EventKind::Snapshot, true, {}, std::move(snap)});
        }
        const int slices = wake ? 2 : 15;
        for (int i = 0; i < slices && !stop_.load(); ++i) {
            {
                std::lock_guard<std::mutex> lock(state_mu_);
                if (impl_ && impl_->sync.consume_wallet_changed()) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

void WalletCore::emit_event(Event event)
{
    Listener listener;
    {
        std::lock_guard<std::mutex> lock(listener_mu_);
        listener = listener_;
    }
    if (listener) {
        listener(std::move(event));
    }
}

void WalletCore::set_busy(bool busy)
{
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        busy_ = busy;
    }
    emit_event(Event{EventKind::BusyChanged, true, {}, build_snapshot()});
}

Snapshot WalletCore::build_snapshot() const
{
    Snapshot snap;
    snap.data_dir = data_dir_;
    snap.exists = wallet_file_exists(data_dir_);

    std::shared_ptr<Impl> impl;
    std::shared_ptr<ltc::Wallet> wallet;
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        snap.busy = busy_;
        if (!impl_) {
            return snap;
        }
        impl = impl_;
        wallet = impl_->wallet;
        snap.open = static_cast<bool>(wallet);
    }

    // Sync status is available before unlock (headers-only warm-up).
    auto st = impl->sync.status();
    snap.sync.running = st.running;
    snap.sync.connected = st.connected;
    snap.sync.phase = ltc::sync_phase_name(st.phase);
    snap.sync.connection = ltc::sync_conn_label(st);
    snap.sync.progress = ltc::sync_progress_label(st);
    snap.sync.detail = st.detail;
    snap.sync.last_error = st.last_error;
    snap.sync.peer_agent = st.peer_agent;
    snap.sync.tip_height = st.tip_height;
    snap.sync.target_height = st.target_height > 0
        ? st.target_height
        : (st.peer_height > 0 ? static_cast<uint32_t>(st.peer_height) : 0);
    snap.sync.rescan_height = st.rescan_height;
    snap.sync.peers = st.peers;
    snap.sync.matched_txs = st.matched_txs;
    snap.sync.rescanning = st.rescanning;
    snap.sync.hard_refresh = st.hard_refresh;

    if (!wallet) {
        return snap;
    }

    // Hold Impl/wallet via shared_ptr so sync can keep running while we read.
    // Do not hold state_mu_ across wallet_mu_ (ingest must not stall the UI poller).
    {
        std::lock_guard<std::mutex> wlock(impl->sync.wallet_mutex());
        st.balance_sats = wallet->balance();
        // Only addresses already issued to the user - not the BIP44 gap look-ahead
        // (4 types × gap 20) kept internally for bloom / SPV watching.
        for (const auto& a : wallet->issued_receive_addresses()) {
            snap.receive_addresses.push_back(a.address);
        }

        snap.tx_history.reserve(wallet->tx_history().size());
        for (const auto& rec : wallet->tx_history()) {
            TxHistoryEntry entry;
            entry.txid = rec.txid;
            entry.address = rec.address;
            entry.amount_sats = rec.amount_sats;
            entry.height = rec.height;
            entry.outgoing = rec.outgoing;
            entry.spent = false;
            entry.outputs = 0;
            snap.tx_history.push_back(std::move(entry));
        }
        std::sort(snap.tx_history.begin(),
                  snap.tx_history.end(),
                  [](const TxHistoryEntry& a, const TxHistoryEntry& b) {
                      // Mempool (height 0) first, then newest confirmed blocks.
                      const bool aMem = a.height == 0;
                      const bool bMem = b.height == 0;
                      if (aMem != bMem) {
                          return aMem;
                      }
                      if (a.height != b.height) {
                          return a.height > b.height;
                      }
                      return a.txid > b.txid;
                  });
    }
    snap.balance_sats = st.balance_sats;
    snap.sync.matched_txs = st.matched_txs;
    return snap;
}

} // namespace waltosh::core
