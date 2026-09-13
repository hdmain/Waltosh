#pragma once

#include "ltc/util/bytes.hpp"

#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ltc {

// Best-effort wipe of sensitive buffers.
inline void secure_wipe(void* p, size_t n) {
  if (!p || n == 0) return;
#if defined(_WIN32)
  SecureZeroMemory(p, n);
#else
  volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
  while (n--) *v++ = 0;
#endif
}

inline void secure_wipe(Bytes& b) {
  if (!b.empty()) secure_wipe(b.data(), b.size());
  b.clear();
  b.shrink_to_fit();
}

inline void secure_wipe(std::string& s) {
  if (!s.empty()) secure_wipe(s.data(), s.size());
  s.clear();
  s.shrink_to_fit();
}

}  // namespace ltc
