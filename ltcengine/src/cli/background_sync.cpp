#include "ltc/cli/background_sync.hpp"

#include "ltc/crypto/random.hpp"
#include "ltc/util/bytes.hpp"
#include "ltc/util/error_log.hpp"

#include <algorithm>
#include <chrono>

namespace ltc {

const char* sync_phase_name(SyncStatus::Phase p) {
  switch (p) {
    case SyncStatus::Phase::Stopped:
      return "stopped";
    case SyncStatus::Phase::Connecting:
      return "connecting";
    case SyncStatus::Phase::SyncingHeaders:
      return "syncing headers";
    case SyncStatus::Phase::Watching:
      return "live / watching";
    case SyncStatus::Phase::Paused:
      return "paused";
    case SyncStatus::Phase::Reconnecting:
      return "reconnecting";
    case SyncStatus::Phase::Error:
      return "error";
  }
  return "unknown";
}

std::string sync_conn_label(const SyncStatus& st) {
  if (!st.conn_status.empty()) return st.conn_status;
  switch (st.phase) {
    case SyncStatus::Phase::Connecting:
      return "connecting";
    case SyncStatus::Phase::Reconnecting:
      return "reconnecting";
    case SyncStatus::Phase::Error:
      return "disconnected";
    case SyncStatus::Phase::Stopped:
      return "disconnected";
    case SyncStatus::Phase::Paused:
      return st.connected ? "connected (paused)" : "paused";
    case SyncStatus::Phase::SyncingHeaders:
    case SyncStatus::Phase::Watching:
      return st.connected ? "connected" : "connecting";
  }
  return "unknown";
}

std::string format_eta(int64_t seconds) {
  if (seconds < 0) return "eta …";
  if (seconds < 60) return "eta " + std::to_string(seconds) + "s";
  if (seconds < 3600) {
    int64_t m = seconds / 60;
    int64_t s = seconds % 60;
    if (s == 0) return "eta " + std::to_string(m) + "m";
    return "eta " + std::to_string(m) + "m " + std::to_string(s) + "s";
  }
  int64_t h = seconds / 3600;
  int64_t m = (seconds % 3600) / 60;
  if (m == 0) return "eta " + std::to_string(h) + "h";
  return "eta " + std::to_string(h) + "h " + std::to_string(m) + "m";
}

std::string sync_progress_label(const SyncStatus& st) {
  uint32_t target = st.target_height;
  if (target == 0 && st.peer_height > 0) target = static_cast<uint32_t>(st.peer_height);
  if (st.rescanning) {
    return "rescanning blocks " + std::to_string(st.rescan_height) + "/" +
           std::to_string(st.tip_height ? st.tip_height : target) +
           (st.eta_seconds >= 0 ? " · " + format_eta(st.eta_seconds) : "");
  }
  if (st.phase == SyncStatus::Phase::SyncingHeaders ||
      (st.phase == SyncStatus::Phase::Watching && target > st.tip_height + 10)) {
    if (target == 0) target = st.tip_height;
    std::string s = "downloading blocks " + std::to_string(st.tip_height) + "/" +
                    std::to_string(target);
    if (st.eta_seconds >= 0 || st.headers_per_sec > 0) s += " · " + format_eta(st.eta_seconds);
    return s;
  }
  if (st.phase == SyncStatus::Phase::Watching) {
    return "blocks " + std::to_string(st.tip_height) + "/" +
           std::to_string(target ? target : st.tip_height) + " (synced)";
  }
  if (st.phase == SyncStatus::Phase::Connecting || st.phase == SyncStatus::Phase::Reconnecting) {
    std::string s = "blocks " + std::to_string(st.tip_height) + "/" +
                    (target ? std::to_string(target) : std::string("…"));
    if (st.eta_seconds >= 0) s += " · " + format_eta(st.eta_seconds);
    return s;
  }
  return "blocks " + std::to_string(st.tip_height);
}

BackgroundSync::BackgroundSync() = default;

BackgroundSync::~BackgroundSync() { stop(); }

void BackgroundSync::set_phase(SyncStatus::Phase p, const std::string& detail) {
  std::lock_guard<std::mutex> lock(status_mu_);
  status_.phase = p;
  if (!detail.empty()) status_.detail = detail;
  status_.running = running_.load();
}

void BackgroundSync::reset_rate() {
  rate_sample_height_ = 0;
  rate_ema_ = 0;
  std::lock_guard<std::mutex> lock(status_mu_);
  status_.headers_per_sec = 0;
  status_.eta_seconds = -1;
}

void BackgroundSync::refresh_eta_locked() {
  uint32_t target = status_.target_height;
  if (target == 0 && status_.peer_height > 0) target = static_cast<uint32_t>(status_.peer_height);
  if (target > status_.tip_height && status_.headers_per_sec > 10.0) {
    status_.eta_seconds =
        static_cast<int64_t>((target - status_.tip_height) / status_.headers_per_sec);
  } else if (target <= status_.tip_height) {
    status_.eta_seconds = 0;
  } else {
    status_.eta_seconds = -1;
  }
}

void BackgroundSync::update_tip(uint32_t height, const Hash256& tip) {
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(status_mu_);
  if (rate_sample_height_ > 0 && height > rate_sample_height_) {
    double dt = std::chrono::duration<double>(now - rate_sample_time_).count();
    if (dt >= 0.25) {
      double inst = static_cast<double>(height - rate_sample_height_) / dt;
      rate_ema_ = (rate_ema_ > 0.0) ? (rate_ema_ * 0.65 + inst * 0.35) : inst;
      status_.headers_per_sec = rate_ema_;
      rate_sample_height_ = height;
      rate_sample_time_ = now;
    }
  } else if (height != rate_sample_height_) {
    rate_sample_height_ = height;
    rate_sample_time_ = now;
  }
  status_.tip_height = height;
  status_.tip_hash = to_hex(tip.data(), tip.size(), true);
  if (status_.target_height < height) status_.target_height = height;
  refresh_eta_locked();
}

net::BloomFilter BackgroundSync::build_bloom_locked() {
  // caller holds wallet_mu_
  auto scripts = wallet_->watched_scripts();
  uint32_t n = static_cast<uint32_t>(std::max<size_t>(scripts.size() * 2 + 16, 128));
  net::BloomFilter bloom(n, 0.0001, random_u32(), net::BLOOM_UPDATE_ALL);
  wallet_->fill_bloom(bloom);
  return bloom;
}

void BackgroundSync::start(Wallet* wallet, const std::string& password) {
  if (!wallet) return;
  if (running_.load()) {
    attach_wallet(wallet, password);
    return;
  }
  stop();
  wallet_ = wallet;
  password_ = password;
  data_dir_ = wallet->data_dir();
  stop_ = false;
  pause_ = false;
  bloom_dirty_ = true;
  hard_refresh_ = false;
  last_confirm_rescan_ = {};
  last_tip_verify_ = {};
  last_full_tip_ = 0;
  thread_done_ = false;
  running_ = true;
  set_error_log_dir(data_dir_);
  {
    std::lock_guard<std::mutex> lock(status_mu_);
    status_ = SyncStatus{};
    status_.running = true;
    status_.phase = SyncStatus::Phase::Connecting;
    status_.detail = "starting background sync";
    status_.balance_sats = wallet_->balance();
  }
  thread_ = std::thread([this] { thread_main(); });
}

void BackgroundSync::start_headers_only(const std::string& data_dir) {
  if (data_dir.empty()) return;
  if (running_.load()) return;
  stop();
  {
    std::lock_guard<std::mutex> lock(wallet_mu_);
    wallet_ = nullptr;
    password_.clear();
  }
  data_dir_ = data_dir;
  stop_ = false;
  pause_ = false;
  bloom_dirty_ = false;
  hard_refresh_ = false;
  last_confirm_rescan_ = {};
  last_tip_verify_ = {};
  last_full_tip_ = 0;
  thread_done_ = false;
  running_ = true;
  set_error_log_dir(data_dir_);
  {
    std::lock_guard<std::mutex> lock(status_mu_);
    status_ = SyncStatus{};
    status_.running = true;
    status_.phase = SyncStatus::Phase::Connecting;
    status_.detail = "warming peers & headers (before unlock)";
    status_.balance_sats = 0;
  }
  thread_ = std::thread([this] { thread_main(); });
}

void BackgroundSync::attach_wallet(Wallet* wallet, const std::string& password) {
  if (!wallet) return;
  {
    std::lock_guard<std::mutex> lock(wallet_mu_);
    wallet_ = wallet;
    password_ = password;
    if (data_dir_.empty()) data_dir_ = wallet->data_dir();
  }
  bloom_dirty_ = true;
  {
    std::lock_guard<std::mutex> lock(status_mu_);
    status_.running = true;
    status_.balance_sats = wallet->balance();
    status_.detail = "wallet unlocked - preparing address scan";
  }
}

void BackgroundSync::detach_wallet() {
  {
    std::lock_guard<std::mutex> lock(wallet_mu_);
    wallet_ = nullptr;
    password_.clear();
  }
  bloom_dirty_ = true;
  {
    std::lock_guard<std::mutex> lock(status_mu_);
    status_.balance_sats = 0;
    status_.matched_txs = 0;
    status_.rescanning = false;
    status_.hard_refresh = false;
    status_.detail = "wallet locked - keeping headers warm";
  }
  if (auto* spv = active_spv_.load()) {
    try {
      spv->interrupt();
    } catch (...) {
    }
  }
}

bool BackgroundSync::has_wallet() const {
  std::lock_guard<std::mutex> lock(wallet_mu_);
  return wallet_ != nullptr;
}

void BackgroundSync::stop() {
  stop_ = true;
  pause_ = false;
  // Already idle - do not spin for ~10s waiting on thread_done_.
  if (!running_.load() && thread_done_.load() && !thread_.joinable()) {
    wallet_ = nullptr;
    set_phase(SyncStatus::Phase::Stopped, "sync stopped");
    return;
  }
  // Wait for the sync loop to exit cleanly. Detaching while SpvNode still
  // holds sockets is what made the app "hang" after clicking X.
  for (int i = 0; i < 200 && !thread_done_.load(); ++i) {
    if (auto* spv = active_spv_.load()) {
      try {
        spv->interrupt();
      } catch (...) {
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (thread_.joinable()) {
    if (!thread_done_.load()) {
      // One last interrupt burst, then join anyway (interrupt should unblock recv).
      for (int i = 0; i < 20 && !thread_done_.load(); ++i) {
        if (auto* spv = active_spv_.load()) {
          try {
            spv->interrupt();
          } catch (...) {
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  active_spv_.store(nullptr);
  running_ = false;
  thread_done_ = true;
  set_phase(SyncStatus::Phase::Stopped, "sync stopped");
  wallet_ = nullptr;
}

void BackgroundSync::pause() {
  pause_ = true;
  set_phase(SyncStatus::Phase::Paused, "paused for wallet operation");
}

void BackgroundSync::resume() {
  pause_ = false;
  bloom_dirty_ = true;
}

void BackgroundSync::request_bloom_refresh() { bloom_dirty_ = true; }

void BackgroundSync::request_hard_refresh() {
  hard_refresh_ = true;
  bloom_dirty_ = true;
  {
    std::lock_guard<std::mutex> lock(status_mu_);
    status_.hard_refresh = true;
    status_.detail = "hard refresh queued - waiting for sync loop";
  }
}

void BackgroundSync::notify_wallet_changed() { wallet_changed_.store(true); }

bool BackgroundSync::consume_wallet_changed() { return wallet_changed_.exchange(false); }

SyncStatus BackgroundSync::status() const {
  std::lock_guard<std::mutex> lock(status_mu_);
  return status_;
}

void BackgroundSync::thread_main() {
  struct ActiveGuard {
    BackgroundSync* self;
    ~ActiveGuard() {
      self->active_spv_.store(nullptr);
      self->thread_done_.store(true);
      self->running_.store(false);
    }
  } guard{this};

  while (!stop_.load()) {
    try {
      while (pause_.load() && !stop_.load()) {
        set_phase(SyncStatus::Phase::Paused, "paused");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      if (stop_.load()) break;

      set_phase(SyncStatus::Phase::Connecting, "dialing Litecoin P2P peers");
      reset_rate();
      {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.connected = false;
        status_.conn_status = "connecting";
        status_.peers = 0;
      }
      auto spv = std::make_unique<net::SpvNode>(data_dir_);
      active_spv_.store(spv.get());
      struct ClearSpv {
        std::atomic<net::SpvNode*>* slot;
        ~ClearSpv() { slot->store(nullptr); }
      } clear_spv{&active_spv_};
      if (stop_.load()) break;
      {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.tip_height = spv->tip_height();
        status_.tip_hash = to_hex(spv->tip_hash().data(), spv->tip_hash().size(), true);
        rate_sample_height_ = status_.tip_height;
        rate_sample_time_ = std::chrono::steady_clock::now();
      }
      if (!spv->connect_peers(2, 6)) throw std::runtime_error("no peers available");
      // New peer session - always push bloom again (dirty may be false after prior watch).
      bloom_dirty_ = true;

      {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.peers = static_cast<int>(spv->peer_count());
        status_.connected = true;
        status_.conn_status = "connected";
        if (auto* p = spv->active_peer()) {
          status_.peer_height = p->peer_start_height();
          if (status_.peer_height > 0)
            status_.target_height = static_cast<uint32_t>(status_.peer_height);
          status_.peer_agent = p->peer_user_agent();
          status_.detail = "peers " + std::to_string(spv->peer_count()) + " · " + status_.peer_agent;
          refresh_eta_locked();
        }
      }

      auto on_progress = [this](uint32_t height, const Hash256& tip) {
        update_tip(height, tip);
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.connected = true;
        status_.conn_status = "connected";
        if (status_.headers_per_sec > 0) {
          status_.detail = "height " + std::to_string(height) + " · " +
                           std::to_string(static_cast<int>(status_.headers_per_sec + 0.5)) +
                           " hdr/s · " + format_eta(status_.eta_seconds);
        } else {
          status_.detail = "height " + std::to_string(height);
        }
      };

      set_phase(SyncStatus::Phase::SyncingHeaders, "downloading block headers");
      {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.conn_status = "connected";
        status_.connected = true;
      }
      int stall_retries = 0;
      while (!stop_.load()) {
        while (pause_.load() && !stop_.load()) {
          spv->flush_headers();
          set_phase(SyncStatus::Phase::Paused, "paused");
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (stop_.load()) break;
        int r = spv->sync_headers_round(on_progress, &stop_);
        {
          std::lock_guard<std::mutex> slock(status_mu_);
          status_.tip_height = spv->tip_height();
          status_.tip_hash = to_hex(spv->tip_hash().data(), spv->tip_hash().size(), true);
          status_.connected = true;
          status_.conn_status = "connected";
          status_.peers = static_cast<int>(spv->peer_count());
          if (auto* p = spv->active_peer()) {
            status_.peer_agent = p->peer_user_agent();
            status_.peer_height = p->peer_start_height();
          }
          refresh_eta_locked();
          if (wallet_) {
            // cheap unlocked peek avoided - balance updated less often
          }
        }
        if (r == 1) {
          stall_retries = 0;
          continue;  // full speed - immediately request next batch
        }
        if (r == -1) {
          ++stall_retries;
          if (stall_retries >= 8) throw std::runtime_error("header sync stalled; rotating peer");
          continue;
        }
        // r == 0 caught up
        spv->flush_headers();
        break;
      }
      if (stop_.load()) {
        spv->flush_headers();
        break;
      }

      // Pre-login (or after lock): hold the peer session and keep headers fresh
      // until a wallet is attached - no bloom / private keys involved.
      while (!stop_.load()) {
        bool have_wallet = false;
        {
          std::lock_guard<std::mutex> wlock(wallet_mu_);
          have_wallet = wallet_ != nullptr;
        }
        if (have_wallet) break;

        set_phase(SyncStatus::Phase::Watching, "warming headers - unlock to scan wallet");
        {
          std::lock_guard<std::mutex> lock(status_mu_);
          status_.connected = true;
          status_.conn_status = "connected";
          status_.tip_height = spv->tip_height();
          status_.tip_hash = to_hex(spv->tip_hash().data(), spv->tip_hash().size(), true);
          status_.peers = static_cast<int>(spv->peer_count());
          status_.detail = "headers tip " + std::to_string(spv->tip_height()) +
                           " · unlock wallet to continue";
        }

        while (pause_.load() && !stop_.load()) {
          set_phase(SyncStatus::Phase::Paused, "paused");
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (stop_.load()) break;

        int r = spv->sync_headers_round(on_progress, &stop_);
        {
          std::lock_guard<std::mutex> slock(status_mu_);
          status_.tip_height = spv->tip_height();
          status_.tip_hash = to_hex(spv->tip_hash().data(), spv->tip_hash().size(), true);
          status_.peers = static_cast<int>(spv->peer_count());
          if (auto* p = spv->active_peer()) {
            status_.peer_agent = p->peer_user_agent();
            status_.peer_height = p->peer_start_height();
          }
          refresh_eta_locked();
        }
        if (r == 1) continue;  // more headers available
        // Caught up - wait briefly for unlock / attach_wallet.
        for (int i = 0; i < 40 && !stop_.load(); ++i) {
          {
            std::lock_guard<std::mutex> wlock(wallet_mu_);
            if (wallet_) break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
      if (stop_.load()) {
        spv->flush_headers();
        break;
      }

      {
        std::lock_guard<std::mutex> wlock(wallet_mu_);
        std::lock_guard<std::mutex> slock(status_mu_);
        if (wallet_) status_.balance_sats = wallet_->balance();
      }

      // Catch up: bloom-rescan headers we already have but never filtered.
      {
        bool have_wallet = false;
        {
          std::lock_guard<std::mutex> wlock(wallet_mu_);
          have_wallet = wallet_ != nullptr;
        }
        if (!have_wallet) {
          // Detached again - restart outer loop in headers-only mode.
          continue;
        }
        bloom_dirty_ = true;
        {
          std::lock_guard<std::mutex> wlock(wallet_mu_);
          if (!wallet_) continue;
          auto bloom = build_bloom_locked();
          spv->set_bloom(bloom);
          try {
            spv->send_filterload();
          } catch (const std::exception& e) {
            // No BIP37 peer - rescan_filtered still works via full blocks.
            bloom_dirty_ = true;
            log_error(e.what(), "background_sync/catchup_filterload");
          }
        }

        uint32_t from = 0;
        uint32_t tip = spv->tip_height();
        {
          std::lock_guard<std::mutex> wlock(wallet_mu_);
          if (wallet_) from = wallet_->filter_height();
        }
        // First run / recovery: scan a recent window so deposits during header sync
        // are not skipped forever. Keep this modest (TUI used 500).
        constexpr uint32_t kFirstWindow = 500;
        if (from == 0 && tip > kFirstWindow) from = tip - kFirstWindow;
        else if (from == 0) from = 0;

        // If already at tip, still re-check a small recent window after unlock so
        // the UI shows a real scan and offline deposits near tip are recovered.
        constexpr uint32_t kUnlockTipWindow = 72;
        if (tip > 0 && from >= tip) {
          from = tip > kUnlockTipWindow ? tip - kUnlockTipWindow : 0;
          std::lock_guard<std::mutex> wlock(wallet_mu_);
          if (wallet_) wallet_->rewind_filter_height(from);
        }

        if (from < tip) {
          set_phase(SyncStatus::Phase::Watching, "rescanning filtered blocks");
          {
            std::lock_guard<std::mutex> lock(status_mu_);
            status_.rescanning = true;
            status_.rescan_height = from;
            status_.tip_height = tip;
            status_.detail = "rescan " + std::to_string(from) + ".." + std::to_string(tip);
          }
          auto rescan_progress = [this, tip](uint32_t height, const Hash256&) {
            std::lock_guard<std::mutex> lock(status_mu_);
            status_.rescanning = true;
            status_.rescan_height = height;
            status_.tip_height = tip;
            status_.detail = "rescan " + std::to_string(height) + "/" + std::to_string(tip);
          };
          auto on_tx = [this](const Transaction& tx, uint32_t height, const Hash256&) {
            int64_t bal = 0;
            {
              std::lock_guard<std::mutex> wlock(wallet_mu_);
              if (!wallet_) return;
              // ingest_tx already persists utxos + tx history + meta.
              wallet_->ingest_tx(tx, height);
              bal = wallet_->balance();
            }
            {
              std::lock_guard<std::mutex> slock(status_mu_);
              status_.matched_txs += 1;
              status_.balance_sats = bal;
              status_.detail = "matched tx at height " + std::to_string(height);
            }
            bloom_dirty_ = true;
            notify_wallet_changed();
          };
          try {
            uint32_t scanned =
                spv->rescan_filtered(from, on_tx, rescan_progress, &stop_);
            {
              std::lock_guard<std::mutex> wlock(wallet_mu_);
              if (wallet_) wallet_->set_filter_height(scanned);
            }
            {
              std::lock_guard<std::mutex> wlock(wallet_mu_);
              std::lock_guard<std::mutex> lock(status_mu_);
              status_.rescanning = false;
              status_.rescan_height = scanned;
              if (wallet_) status_.balance_sats = wallet_->balance();
            }
            notify_wallet_changed();
          } catch (const std::exception& e) {
            log_error(e.what(), "background_sync/catchup_rescan");
            std::lock_guard<std::mutex> lock(status_mu_);
            status_.rescanning = false;
            status_.last_error = e.what();
            status_.detail = std::string("catch-up rescan failed: ") + e.what();
            // Continue into the watch loop instead of tearing the session down.
          }
        }
      }
      if (stop_.load()) {
        spv->flush_headers();
        break;
      }

      // Continuous watch loop - same shape as the TUI:
      // bloom → live BIP37 poll → (optional user hard refresh) → headers →
      // bloom gap rescan → status → immediately back to poll.
      // Automatic tip full-block verify / soft-confirm were removed from the hot
      // path; they starved live watching and made the Qt UI feel minutes-behind.
      set_phase(SyncStatus::Phase::Watching, "headers caught up; watching for wallet txs");
      bloom_dirty_ = true;
      while (!stop_.load()) {
        while (pause_.load() && !stop_.load()) {
          set_phase(SyncStatus::Phase::Paused, "paused");
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (stop_.load()) break;

        auto ingest = [this](const Transaction& tx, uint32_t height, const Hash256&) {
          int64_t bal = 0;
          {
            std::lock_guard<std::mutex> wlock(wallet_mu_);
            if (!wallet_) return;
            // ingest_tx already persists utxos + tx history + meta - do not call
            // full wallet->save() under this lock (blocks UI snapshot reads).
            wallet_->ingest_tx(tx, height);
            bal = wallet_->balance();
          }
          {
            std::lock_guard<std::mutex> slock(status_mu_);
            status_.matched_txs += 1;
            status_.balance_sats = bal;
            status_.detail = "matched tx at height " + std::to_string(height);
          }
          bloom_dirty_ = true;
          notify_wallet_changed();
        };

        auto refresh_progress = [this, &on_progress](uint32_t height, const Hash256& tip) {
          on_progress(height, tip);
          std::lock_guard<std::mutex> lock(status_mu_);
          status_.rescan_height = height;
          if (status_.hard_refresh || status_.rescanning) {
            status_.detail = (status_.hard_refresh ? "hard refresh @ " : "rescan @ ") +
                             std::to_string(height) + "/" + std::to_string(status_.tip_height);
          }
        };

        if (bloom_dirty_.exchange(false) || !spv->has_bloom()) {
          try {
            std::lock_guard<std::mutex> wlock(wallet_mu_);
            if (!wallet_) break;
            auto bloom = build_bloom_locked();
            spv->set_bloom(bloom);
            try {
              spv->send_filterload();
              set_phase(SyncStatus::Phase::Watching, "bloom filter refreshed");
            } catch (const std::exception& e) {
              bloom_dirty_ = true;
              log_error(e.what(), "background_sync/filterload");
              set_phase(SyncStatus::Phase::Watching, "watching without bloom peer");
            }
          } catch (const std::exception& e) {
            bloom_dirty_ = true;
            throw;
          }
        }

        // Live BIP37 window - primary path for instant balance updates.
        // Pass bloom_dirty_ as wake so a new address reloads the filter immediately
        // instead of waiting out the full poll window.
        try {
          spv->poll_network(ingest, on_progress, 15000, &stop_, &bloom_dirty_);
        } catch (const std::exception& e) {
          log_error(e.what(), "background_sync/poll");
          spv->rotate_peer(true);
        }

        // User hard refresh only - never runs on every cycle.
        if (hard_refresh_.load() && !stop_.load() && !pause_.load()) {
          hard_refresh_.store(false);
          const uint32_t tip = spv->tip_height();
          constexpr uint32_t kHardLookback = 2016;
          constexpr uint32_t kTipFull = 32;
          const uint32_t refresh_from = tip > kHardLookback ? tip - kHardLookback : 0;
          const uint32_t tip_full_from = tip > kTipFull ? tip - kTipFull : 0;
          bloom_dirty_ = true;
          try {
            std::lock_guard<std::mutex> wlock(wallet_mu_);
            if (wallet_) {
              auto bloom = build_bloom_locked();
              spv->set_bloom(bloom);
              try {
                spv->send_filterload();
              } catch (const std::exception& e) {
                log_error(e.what(), "background_sync/hard_refresh_filterload");
              }
            }
          } catch (const std::exception& e) {
            bloom_dirty_ = true;
            log_error(e.what(), "background_sync/hard_refresh_bloom");
          }

          set_phase(SyncStatus::Phase::Watching, "hard refresh: rescanning recent blocks");
          {
            std::lock_guard<std::mutex> lock(status_mu_);
            status_.rescanning = true;
            status_.hard_refresh = true;
            status_.rescan_height = refresh_from;
            status_.tip_height = tip;
            status_.detail = "hard refresh " + std::to_string(refresh_from) + ".." +
                             std::to_string(tip);
          }
          try {
            {
              std::lock_guard<std::mutex> wlock(wallet_mu_);
              if (wallet_) wallet_->rewind_filter_height(refresh_from);
            }
            uint32_t scanned =
                spv->rescan_filtered(refresh_from, ingest, refresh_progress, &stop_, false);
            if (!stop_.load() && tip > tip_full_from) {
              {
                std::lock_guard<std::mutex> wlock(wallet_mu_);
                if (wallet_) wallet_->rewind_filter_height(tip_full_from);
              }
              scanned = spv->rescan_filtered(tip_full_from, ingest, refresh_progress, &stop_, true);
            }
            {
              std::lock_guard<std::mutex> wlock(wallet_mu_);
              if (wallet_) wallet_->set_filter_height(scanned);
            }
            {
              std::lock_guard<std::mutex> wlock(wallet_mu_);
              std::lock_guard<std::mutex> lock(status_mu_);
              status_.rescanning = false;
              status_.hard_refresh = false;
              status_.detail = "hard refresh complete · tip " + std::to_string(spv->tip_height());
              if (wallet_) status_.balance_sats = wallet_->balance();
            }
            last_confirm_rescan_ = std::chrono::steady_clock::now();
            last_tip_verify_ = last_confirm_rescan_;
            last_full_tip_ = spv->tip_height();
            notify_wallet_changed();
          } catch (const std::exception& e) {
            log_error(e.what(), "background_sync/hard_refresh");
            std::lock_guard<std::mutex> lock(status_mu_);
            status_.rescanning = false;
            status_.hard_refresh = false;
            status_.last_error = e.what();
            status_.detail = std::string("hard refresh failed: ") + e.what();
          }
        }

        // TUI hot path: advance headers, then bloom-rescan only the unscanned gap.
        if (!stop_.load() && !pause_.load()) {
          set_phase(SyncStatus::Phase::SyncingHeaders, "checking for new headers");
          int stall = 0;
          while (!stop_.load() && !pause_.load() && !hard_refresh_.load()) {
            int r = spv->sync_headers_round(on_progress, &stop_);
            if (r == 1) {
              stall = 0;
              continue;
            }
            if (r == -1) {
              if (++stall >= 5) break;
              continue;
            }
            break;
          }
          spv->flush_headers();

          uint32_t from = 0;
          uint32_t tip = spv->tip_height();
          {
            std::lock_guard<std::mutex> wlock(wallet_mu_);
            if (wallet_) from = wallet_->filter_height();
          }

          if (from < tip && !stop_.load() && !pause_.load() && !hard_refresh_.load()) {
            try {
              {
                std::lock_guard<std::mutex> lock(status_mu_);
                status_.rescanning = true;
                status_.rescan_height = from;
                status_.tip_height = tip;
                status_.detail = "rescan " + std::to_string(from) + ".." + std::to_string(tip);
              }
              uint32_t scanned =
                  spv->rescan_filtered(from, ingest, refresh_progress, &stop_, false);
              {
                std::lock_guard<std::mutex> wlock(wallet_mu_);
                if (wallet_) wallet_->set_filter_height(scanned);
              }
              {
                std::lock_guard<std::mutex> lock(status_mu_);
                status_.rescanning = false;
              }
              last_full_tip_ = tip;
              notify_wallet_changed();
            } catch (const std::exception& e) {
              log_error(e.what(), "background_sync/incremental_rescan");
              std::lock_guard<std::mutex> lock(status_mu_);
              status_.rescanning = false;
            }
          }

          // No-bloom safety net only: rare, tiny full-block tip check so we do not
          // starve the next poll_network round the way the old always-on verify did.
          {
            bool bloom_ok = false;
            if (auto* p = spv->active_peer()) bloom_ok = p->peer_supports_bloom();
            const auto now = std::chrono::steady_clock::now();
            constexpr auto kNoBloomVerifyInterval = std::chrono::seconds(120);
            constexpr uint32_t kNoBloomWindow = 3;
            tip = spv->tip_height();
            const bool due = last_tip_verify_.time_since_epoch().count() == 0 ||
                             now - last_tip_verify_ >= kNoBloomVerifyInterval;
            if (!bloom_ok && due && tip > 0 && !stop_.load() && !pause_.load() &&
                !hard_refresh_.load()) {
              const uint32_t verify_from = tip > kNoBloomWindow ? tip - kNoBloomWindow : 0;
              try {
                {
                  std::lock_guard<std::mutex> lock(status_mu_);
                  status_.rescanning = true;
                  status_.rescan_height = verify_from;
                  status_.tip_height = tip;
                  status_.detail = "no-bloom tip check";
                }
                {
                  std::lock_guard<std::mutex> wlock(wallet_mu_);
                  if (wallet_) wallet_->rewind_filter_height(verify_from);
                }
                uint32_t scanned = spv->rescan_filtered(verify_from, ingest, refresh_progress,
                                                        &stop_, true);
                {
                  std::lock_guard<std::mutex> wlock(wallet_mu_);
                  if (wallet_) wallet_->set_filter_height(scanned);
                }
                {
                  std::lock_guard<std::mutex> lock(status_mu_);
                  status_.rescanning = false;
                }
                last_tip_verify_ = now;
                last_full_tip_ = tip;
                notify_wallet_changed();
              } catch (const std::exception& e) {
                log_error(e.what(), "background_sync/tip_verify");
                std::lock_guard<std::mutex> lock(status_mu_);
                status_.rescanning = false;
              }
            }
          }

          set_phase(SyncStatus::Phase::Watching,
                    hard_refresh_.load() ? "hard refresh queued…"
                                         : "live / watching mempool+blocks");
        }

        {
          std::lock_guard<std::mutex> wlock(wallet_mu_);
          std::lock_guard<std::mutex> slock(status_mu_);
          if (wallet_) status_.balance_sats = wallet_->balance();
          status_.tip_height = spv->tip_height();
          status_.tip_hash = to_hex(spv->tip_hash().data(), spv->tip_hash().size(), true);
          status_.peers = static_cast<int>(spv->peer_count());
          if (auto* p = spv->active_peer()) {
            status_.peer_agent = p->peer_user_agent();
            status_.peer_height = p->peer_start_height();
            const bool keepDetail =
                status_.hard_refresh || status_.rescanning ||
                status_.detail.rfind("hard refresh complete", 0) == 0 ||
                status_.detail.rfind("hard refresh failed", 0) == 0 ||
                status_.detail.rfind("matched tx", 0) == 0;
            if (!keepDetail) {
              status_.detail =
                  std::string(p->peer_supports_bloom()
                                  ? "bloom peer · "
                                  : "no-bloom · P2P relay mempool · ") +
                  status_.peer_agent;
            }
          }
        }
      }
    } catch (const std::exception& e) {
      log_error(e.what(), "background_sync");
      {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.phase = SyncStatus::Phase::Error;
        status_.last_error = e.what();
        status_.detail = e.what();
        status_.connected = false;
        status_.conn_status = "disconnected";
        status_.peers = 0;
        status_.rescanning = false;
        // Re-queue hard refresh after reconnect if it was interrupted.
        if (status_.hard_refresh || hard_refresh_.load()) {
          hard_refresh_.store(true);
          status_.hard_refresh = true;
          status_.detail = std::string("hard refresh interrupted - retrying: ") + e.what();
        }
      }
      set_phase(SyncStatus::Phase::Reconnecting, std::string("retry in 2s: ") + e.what());
      {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.conn_status = "reconnecting";
      }
      for (int i = 0; i < 20 && !stop_.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } catch (...) {
      log_error("unknown non-std exception", "background_sync");
      {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.phase = SyncStatus::Phase::Error;
        status_.last_error = "unknown exception";
        status_.detail = "unknown exception";
        status_.connected = false;
        status_.conn_status = "disconnected";
        status_.peers = 0;
        status_.rescanning = false;
        if (status_.hard_refresh || hard_refresh_.load()) {
          hard_refresh_.store(true);
          status_.hard_refresh = true;
        }
      }
      set_phase(SyncStatus::Phase::Reconnecting, "retry in 2s: unknown exception");
      {
        std::lock_guard<std::mutex> lock(status_mu_);
        status_.conn_status = "reconnecting";
      }
      for (int i = 0; i < 20 && !stop_.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  running_ = false;
  set_phase(SyncStatus::Phase::Stopped, "sync thread exited");
}

}  // namespace ltc
