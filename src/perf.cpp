// ncaabb10 - frame pacing statistics. With NCAABB10_PERF=1, every 5 seconds logs the guest present
// rate (VdSwap calls), frame time average / 95th percentile / max, and process CPU use in cores.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rex/logging/log_category.h>
#include <rex/ppc/hook.h>
#include <rex/types.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <dlfcn.h>
#include <sys/resource.h>
#endif

namespace {

using Func = void (*)(PPCContext&, uint8_t*);
using Clock = std::chrono::steady_clock;

Func Original(const char* name) {
#if defined(_WIN32)
  HMODULE modules[512];
  DWORD bytes = 0;
  if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) {
    for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i) {
      if (modules[i] == GetModuleHandleW(nullptr)) {
        continue;
      }
      if (auto found = reinterpret_cast<Func>(GetProcAddress(modules[i], name))) {
        return found;
      }
    }
  }
  return nullptr;
#else
  return reinterpret_cast<Func>(dlsym(RTLD_NEXT, name));
#endif
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
  static std::unordered_map<DWORD, double> previous;
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
std::string TopThreads(double) { return {}; }
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
            "{} frames over 33.4 ms, cpu {:.2f} cores",
            n / window_s, total / n, sorted[std::min(n - 1, n * 95 / 100)], sorted.back(),
            std::count_if(sorted.begin(), sorted.end(), [](double v) { return v > 33.4; }),
            (cpu - window_cpu) / window_s);
        REXLOG_INFO("ncaabb10: perf threads {}", threads);
      }
      window_start = now;
      window_cpu = cpu;
    }
  }
};

FrameStats g_stats;

}  // namespace

// VdSwap is the title's present: one call per guest frame.
REX_HOOK_RAW(__imp__VdSwap) {
  static Func original = nullptr;
  if (!original) {
    original = Original("__imp__VdSwap");
  }
  if (PerfEnabled()) {
    g_stats.OnPresent();
  }
  original(ctx, base);
}
