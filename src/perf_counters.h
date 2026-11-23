// ncaabb10 - counters the hooks update and perf.cpp reports every 5 seconds (NCAABB10_PERF=1).
#pragma once

#include <atomic>
#include <cstdint>

namespace ncaabb10::perf {

struct Counters {
  std::atomic<uint64_t> reads{0};         // NtReadFile / NtReadFileScatter calls
  std::atomic<uint64_t> read_bytes{0};    // bytes those calls returned
  std::atomic<uint64_t> read_micros{0};   // host time spent inside them
  std::atomic<uint64_t> sleeps{0};        // KeDelayExecutionThread calls
  std::atomic<uint64_t> sleep_micros{0};  // time those calls asked for
  std::atomic<uint64_t> gpu_waits{0};     // GPU fence polls that slept (gpu_wait.cpp)
};

inline Counters& Get() {
  static Counters counters;
  return counters;
}

}  // namespace ncaabb10::perf
