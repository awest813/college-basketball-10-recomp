// ncaabb10 - frame pacing statistics. With NCAABB10_PERF=1, every 5 seconds logs the guest present
// rate (VdSwap calls), frame time average / 95th percentile / max, and process CPU use in cores.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rex/logging/log_category.h>
#include <rex/ppc/hook.h>
#include <rex/types.h>

#include "perf_counters.h"

#include "sdk_original.h"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/resource.h>
#endif

namespace {

// The OS thread id, as the log and the profiler show threads.
uint64_t LogThreadId() {
#if defined(_WIN32)
  return GetCurrentThreadId();
#else
  return static_cast<uint64_t>(syscall(SYS_gettid));  // gettid() needs glibc 2.30.
#endif
}


using Func = ncaabb10::KernelFunc;
using ncaabb10::CallOriginal;
using ncaabb10::Original;
using Clock = std::chrono::steady_clock;

// Return addresses up the guest stack: functions save LR at (entry r1 - 8) and keep the back chain
// at 0(r1), so a frame's LR is at (back chain - 8).
std::string GuestStack(const uint8_t* base, uint32_t sp, uint32_t lr) {
  auto load32 = [base](uint32_t address) {
    uint32_t value;
    std::memcpy(&value, base + address, sizeof(value));
    return rex::byte_swap(value);
  };
  std::string out = std::format("{:08X}", lr);
  for (int depth = 0; depth < 12 && sp >= 0x10000 && !(sp & 3); ++depth) {
    const uint32_t back = load32(sp);
    if (back <= sp || back - sp > 0x100000) {
      break;
    }
    const uint32_t saved = load32(back - 8);
    if (saved < 0x82000000 || saved >= 0x84000000) {
      break;
    }
    out += std::format(" <- {:08X}", saved);
    sp = back;
  }
  return out;
}

bool PerfEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("NCAABB10_PERF");
    return value && *value && *value != '0';
  }();
  return enabled;
}

// Process CPU time (user + kernel) in seconds.
double ProcessCpuSeconds() {
#if defined(_WIN32)
  FILETIME creation, exit, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
    return 0.0;
  }
  auto to_seconds = [](const FILETIME& t) {
    return (static_cast<uint64_t>(t.dwHighDateTime) << 32 | t.dwLowDateTime) * 1e-7;
  };
  return to_seconds(kernel) + to_seconds(user);
#else
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 1e-6 + usage.ru_stime.tv_sec +
         usage.ru_stime.tv_usec * 1e-6;
#endif
}

#if defined(_WIN32)
// CPU use of the busiest threads since the last call, as "name 0.97, name 0.40, ...".
std::string TopThreads(double window_s) {
  // Never destroyed: the monitor thread may still be here while the exit runs static destructors.
  static auto& previous = *new std::unordered_map<DWORD, double>;
  std::unordered_map<DWORD, double> current;
  std::vector<std::pair<double, std::string>> busy;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return {};
  }
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  const DWORD pid = GetCurrentProcessId();
  for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != pid) {
      continue;
    }
    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
    if (!thread) {
      continue;
    }
    FILETIME creation, exit, kernel, user;
    if (GetThreadTimes(thread, &creation, &exit, &kernel, &user)) {
      auto to_seconds = [](const FILETIME& t) {
        return (static_cast<uint64_t>(t.dwHighDateTime) << 32 | t.dwLowDateTime) * 1e-7;
      };
      const double cpu = to_seconds(kernel) + to_seconds(user);
      current[entry.th32ThreadID] = cpu;
      auto it = previous.find(entry.th32ThreadID);
      const double used = (cpu - (it != previous.end() ? it->second : cpu)) / window_s;
      if (used > 0.05) {
        std::string name = std::to_string(entry.th32ThreadID);
        PWSTR description = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread, &description)) && description && *description) {
          name.clear();
          for (const wchar_t* c = description; *c && name.size() < 24; ++c) {
            name.push_back(*c < 128 ? static_cast<char>(*c) : '?');
          }
        }
        if (description) {
          LocalFree(description);
        }
        busy.emplace_back(used, name);
      }
    }
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
  previous = std::move(current);
  std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::string out;
  for (size_t i = 0; i < busy.size() && i < 6; ++i) {
    out += std::format("{}{} {:.2f}", i ? ", " : "", busy[i].second, busy[i].first);
  }
  return out;
}
#else
// The same from /proc/self/task: each thread's user and system time (stat fields 14 and 15, in
// clock ticks) and its name (comm, at most 15 characters).
std::string TopThreads(double window_s) {
  namespace fs = std::filesystem;
  static auto& previous = *new std::unordered_map<std::string, double>;  // Never destroyed.
  static const double tick = 1.0 / static_cast<double>(sysconf(_SC_CLK_TCK));
  std::unordered_map<std::string, double> current;
  std::vector<std::pair<double, std::string>> busy;
  std::error_code ec;
  for (fs::directory_iterator it("/proc/self/task", ec), end; !ec && it != end; it.increment(ec)) {
    const std::string tid = it->path().filename().string();
    std::ifstream stat_file(it->path() / "stat");
    std::string stat;
    std::getline(stat_file, stat);
    // The name in parentheses may hold spaces; the fields after its closing parenthesis do not.
    const size_t close = stat.rfind(')');
    if (close == std::string::npos) {
      continue;
    }
    std::istringstream fields(stat.substr(close + 1));
    std::string field;
    unsigned long long utime = 0, stime = 0;
    for (int i = 3; i <= 15 && fields >> field; ++i) {  // Field 3 is the state.
      if (i == 14) {
        utime = std::strtoull(field.c_str(), nullptr, 10);
      } else if (i == 15) {
        stime = std::strtoull(field.c_str(), nullptr, 10);
      }
    }
    const double cpu = static_cast<double>(utime + stime) * tick;
    current[tid] = cpu;
    auto prev = previous.find(tid);
    const double used = (cpu - (prev != previous.end() ? prev->second : cpu)) / window_s;
    if (used > 0.05) {
      std::ifstream comm_file(it->path() / "comm");
      std::string name;
      std::getline(comm_file, name);
      busy.emplace_back(used, name.empty() ? tid : name);
    }
  }
  previous = std::move(current);
  std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::string out;
  for (size_t i = 0; i < busy.size() && i < 6; ++i) {
    out += std::format("{}{} {:.2f}", i ? ", " : "", busy[i].second, busy[i].first);
  }
  return out;
}
#endif

// The present hook only appends a timestamp; a monitor thread does the statistics, the thread
// snapshot and the logging every 5 seconds, so measuring does not delay the title's frames.
struct FrameStats {
  std::mutex mutex;
  Clock::time_point last_present{};
  std::vector<double> intervals_ms;
  std::once_flag monitor_started;

  void OnPresent() {
    const auto now = Clock::now();
    {
      std::lock_guard lock(mutex);
      if (last_present != Clock::time_point{}) {
        intervals_ms.push_back(
            std::chrono::duration<double, std::milli>(now - last_present).count());
      }
      last_present = now;
    }
    std::call_once(monitor_started, [this] { std::thread([this] { Monitor(); }).detach(); });
  }

  void Monitor() {
    auto window_start = Clock::now();
    double window_cpu = ProcessCpuSeconds();
    TopThreads(1.0);  // Prime the per-thread baseline.
    std::vector<double> sorted;
    auto& counters = ncaabb10::perf::Get();
    uint64_t last_reads = 0, last_bytes = 0, last_read_us = 0, last_sleeps = 0, last_sleep_us = 0,
             last_gpu_waits = 0;
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      const auto now = Clock::now();
      const double window_s = std::chrono::duration<double>(now - window_start).count();
      {
        std::lock_guard lock(mutex);
        sorted.swap(intervals_ms);
        intervals_ms.clear();
      }
      const double cpu = ProcessCpuSeconds();
      const std::string threads = TopThreads(window_s);
      if (!sorted.empty()) {
        std::sort(sorted.begin(), sorted.end());
        double total = 0.0;
        for (double v : sorted) {
          total += v;
        }
        const size_t n = sorted.size();
        REXLOG_INFO(
            "ncaabb10: perf {:.1f} fps, frame avg {:.2f} ms p95 {:.2f} ms max {:.2f} ms, "
            "{} frames over 33.4 ms, cpu {:.2f} cores, {} frames over 17.5 ms",
            n / window_s, total / n, sorted[std::min(n - 1, n * 95 / 100)], sorted.back(),
            std::count_if(sorted.begin(), sorted.end(), [](double v) { return v > 33.4; }),
            (cpu - window_cpu) / window_s,
            std::count_if(sorted.begin(), sorted.end(), [](double v) { return v > 17.5; }));
        REXLOG_INFO("ncaabb10: perf threads {}", threads);
      }
      const uint64_t reads = counters.reads.load(), bytes = counters.read_bytes.load(),
                     read_us = counters.read_micros.load(), sleeps = counters.sleeps.load(),
                     sleep_us = counters.sleep_micros.load(), gpu_waits = counters.gpu_waits.load();
      REXLOG_INFO(
          "ncaabb10: perf io {} reads {:.1f} MB/s, {:.0f} ms reading; {} guest sleeps asking "
          "{:.0f} ms; {} gpu wait sleeps",
          reads - last_reads, (bytes - last_bytes) / window_s / 1e6, (read_us - last_read_us) / 1e3,
          sleeps - last_sleeps, (sleep_us - last_sleep_us) / 1e3, gpu_waits - last_gpu_waits);
      last_reads = reads, last_bytes = bytes, last_read_us = read_us, last_sleeps = sleeps,
      last_sleep_us = sleep_us, last_gpu_waits = gpu_waits;
      window_start = now;
      window_cpu = cpu;
    }
  }
};

// Never destroyed: the monitor thread is detached and may still run while statics are torn down.
FrameStats& g_stats = *new FrameStats;

}  // namespace

// KeDelayExecutionThread(mode, alertable, LARGE_INTEGER* interval): the title's Sleep. Counted so
// the perf log shows how much of a load is spent sleeping.
REX_HOOK_RAW(__imp__KeDelayExecutionThread) {
  static const Func original = Original("__imp__KeDelayExecutionThread");
  if (PerfEnabled() && ctx.r5.u32) {
    uint64_t raw;
    std::memcpy(&raw, base + ctx.r5.u32, sizeof(raw));
    const int64_t interval = static_cast<int64_t>(rex::byte_swap(raw));
    auto& counters = ncaabb10::perf::Get();
    const uint64_t n = counters.sleeps.fetch_add(1, std::memory_order_relaxed);
    if (interval < 0) {  // Relative, in 100 ns units.
      counters.sleep_micros.fetch_add(static_cast<uint64_t>(-interval / 10),
                                      std::memory_order_relaxed);
    }
    // Zero length sleeps are yields; the loader thread spins on them while a load runs
    // (sub_82275138 / sub_82275020). Log each spin episode's length: those are the loads.
    if (interval == 0) {
      thread_local Clock::time_point episode_start{}, episode_last{};
      thread_local uint64_t episode_yields = 0;
      const auto now = Clock::now();
      if (now - episode_last > std::chrono::milliseconds(500)) {
        const double seconds = std::chrono::duration<double>(episode_last - episode_start).count();
        if (episode_yields > 10000 && seconds > 0.5) {
          REXLOG_INFO("ncaabb10: perf load spin {:.2f} s, {} yields (thread {})", seconds,
                      episode_yields, LogThreadId());
        }
        episode_start = now;
        episode_yields = 0;
      }
      episode_last = now;
      ++episode_yields;
      if ((n % 4000000) == 0) {
        REXLOG_INFO("ncaabb10: perf zero sleep #{} thread {} stack {}", n, LogThreadId(),
                    GuestStack(base, ctx.r1.u32, ctx.lr));
      }
    }
  }
  CallOriginal(original, ctx, base);
}

// VdSwap is the title's present: one call per guest frame.
REX_HOOK_RAW(__imp__VdSwap) {
  static const Func original = Original("__imp__VdSwap");
  if (PerfEnabled()) {
    g_stats.OnPresent();
  }
  CallOriginal(original, ctx, base);
}
