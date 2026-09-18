# WALTOSH

Litecoin SPV wallet for **Windows** and **Linux**.

- **GUI**: DRMSketch-based Qt 6 UI (Qlementine, system backdrop where available)
- **Engine**: [ltcengine](./ltcengine) - BIP39/BIP32 keys, encrypted wallet, pruned P2P SPV sync

No explorers or Electrum servers. Sync talks to the Litecoin network on port 9333.

## Layout

```
Waltosh/
  UI/           Qt GUI (MainWindow, CoreBridge, backdrop)
  core/         waltosh_core - wallet engine, no Qt Widgets
  ltcengine/    Litecoin crypto / SPV library + CLI
  cmake/        third-party patches
  scripts/      run / deploy helpers
  CMakeLists.txt
```

## Architecture

```
UI thread (Qt)          CoreBridge          waltosh_core                 ltcengine
MainWindow  <queued>--  CoreBridge  --jobs-> WalletCore worker thread -> Wallet / send
                              ^                    |
                              |              poller thread (snapshots)
                              |                    |
                              +-- events ----------+
                                                   BackgroundSync thread (P2P)
```

- `core/` has **no Qt Widgets** - only the wallet engine and threads
- `UI/` paints and posts requests; it never calls ltcengine directly
- Create / unlock / send / address work runs on the **worker** thread
- SPV networking runs on **BackgroundSync**'s thread

## Requirements

- CMake 3.21+
- Git (FetchContent: Qlementine + libsecp256k1)
- Qt 6 (Widgets + Svg)
- C++20 compiler

## Build

### Windows (Qt MinGW)

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.9.2\mingw_64\bin;" + $env:PATH
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.9.2/mingw_64" `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe"
cmake --build build -j
.\scripts\run-windows.cmd
```

Binary: `build/Waltosh.exe`

CI builds a Windows **Inno Setup** installer (`Waltosh-*-windows-x64-setup.exe`) and a portable zip. Tag a version (`v0.1.0`) to publish them on GitHub Releases.

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j
./build/Waltosh

# Optional .deb (Ubuntu 22.04+ / Debian with Qt 6.2+)
cd build && cpack -G DEB
```

The `.deb` Depends accept both classic Qt6 packages (`libqt6gui6`) and Ubuntu 24.04+ `*t64` names. CI builds the package on Ubuntu 22.04 for older glibc compatibility.
## Wallet data

- Windows: `%AppData%/WALTOSH/wallet`
- Linux: `~/.local/share/WALTOSH/wallet`

## Notes

- First sync downloads ~2M+ headers; later runs resume from disk.
- Backup the mnemonic shown once at create time (confirm a few words); Waltosh does not store it on disk.
- Wallet file uses Argon2id + AES-256-CBC with HMAC (AEAD). Legacy wallets upgrade on unlock.
- Header sync checks Litecoin checkpoints and scrypt PoW after the last checkpoint; tips are cross-checked across peers.
- Backdrop effects are best-effort; unsupported environments use an opaque Qt theme.
