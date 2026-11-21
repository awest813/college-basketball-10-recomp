// ncaabb10 - a small sampling profiler (Windows). With NCAABB10_PROFILE=1, 100 times a second it
// suspends each thread that used CPU since the last tick, reads its instruction pointer, resumes
// it and credits the cycles the thread used to that location. Every 10 seconds it logs the busiest recompiled guest functions overall and for
// the three busiest threads. Addresses in the recompiled code section resolve to sub_XXXXXXXX
// through the generated guest to host function table; others count under their module.

#if defined(_WIN32)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <rex/logging/log_category.h>
#include <rex/ppc/recompiled_code.h>

namespace {

struct GuestFunction {
  uintptr_t host;
  uint32_t guest;
};

struct ThreadSlot {
  HANDLE handle = nullptr;
  uint64_t last_cycles = 0;
  std::string name;
  std::unordered_map<uint64_t, uint64_t> hits;  // key -> CPU cycles; key: guest address or module base | 1
  uint64_t samples = 0;                          // CPU cycles sampled
};

// About 0.1 ms of a 3-4 GHz core: below this a thread mostly waited during the 10 ms tick.
constexpr uint64_t kMinCycles = 400000;

class Sampler {
 public:
  void Run() {
    BuildFunctionTable();
    REXLOG_INFO("ncaabb10: sampler on, {} guest functions, code {:#x}-{:#x}", functions_.size(),
                code_begin_, code_end_);
    const DWORD self = GetCurrentThreadId();
    auto window_start = std::chrono::steady_clock::now();
    auto last_refresh = window_start - std::chrono::seconds(10);
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    while (true) {
      const auto now = std::chrono::steady_clock::now();
      if (now - last_refresh > std::chrono::seconds(1)) {
        RefreshThreads(self);
        last_refresh = now;
      }
      for (auto& [tid, slot] : threads_) {
        ULONG64 cycles = 0;
        if (!QueryThreadCycleTime(slot.handle, &cycles)) {
          continue;
        }
        const uint64_t used = cycles - slot.last_cycles;
        slot.last_cycles = cycles;
        if (used < kMinCycles) {
          continue;  // Waiting or nearly idle since the last tick: not worth suspending.
        }
        if (SuspendThread(slot.handle) == static_cast<DWORD>(-1)) {
          continue;
        }
        const BOOL got = GetThreadContext(slot.handle, &context);
        ResumeThread(slot.handle);
        if (got) {
          // Weight by the cycles used since the last tick, so shares are shares of CPU time.
          slot.hits[Key(context.Rip)] += used;
          slot.samples += used;
        }
      }
      if (now - window_start > std::chrono::seconds(10)) {
        Report();
        window_start = now;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

 private:
  void BuildFunctionTable() {
    for (const PPCFuncMapping* m = kPPCFuncMappings; m->address; ++m) {
      functions_.push_back({reinterpret_cast<uintptr_t>(m->host), static_cast<uint32_t>(m->address)});
    }
    std::sort(functions_.begin(), functions_.end(),
              [](const GuestFunction& a, const GuestFunction& b) { return a.host < b.host; });
    // The recompiled code section of the executable.
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
      if (std::strncmp(reinterpret_cast<const char*>(section->Name), "rexguest", 8) == 0) {
        code_begin_ = reinterpret_cast<uintptr_t>(base) + section->VirtualAddress;
        code_end_ = code_begin_ + section->Misc.VirtualSize;
      }
    }
  }

  // A guest address for recompiled code, otherwise the module base with the low bit set.
  uint64_t Key(uintptr_t rip) const {
    if (rip >= code_begin_ && rip < code_end_) {
      auto it = std::upper_bound(functions_.begin(), functions_.end(), rip,
                                 [](uintptr_t value, const GuestFunction& f) { return value < f.host; });
      if (it != functions_.begin()) {
        return std::prev(it)->guest;
      }
    }
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(rip), &module);
    return reinterpret_cast<uint64_t>(module) | 1;
  }

  std::string Name(uint64_t key) {
    if (!(key & 1)) {
      return std::format("sub_{:08X}", key);
    }
    wchar_t path[MAX_PATH] = {};
    HMODULE module = reinterpret_cast<HMODULE>(key & ~uint64_t(1));
    if (!module || !GetModuleFileNameW(module, path, MAX_PATH)) {
      return "<unknown>";
    }
    std::string out;
    const wchar_t* file = wcsrchr(path, L'\\');
    for (const wchar_t* c = file ? file + 1 : path; *c; ++c) {
      out.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    }
    return module == GetModuleHandleW(nullptr) ? out + "(host)" : out;
  }

  void RefreshThreads(DWORD self) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
      return;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    const DWORD pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
      if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self ||
          threads_.contains(entry.th32ThreadID)) {
        continue;
      }
      HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                 FALSE, entry.th32ThreadID);
      if (!handle) {
        continue;
      }
      ThreadSlot slot;
      slot.handle = handle;
      slot.name = std::to_string(entry.th32ThreadID);
      PWSTR description = nullptr;
      if (SUCCEEDED(GetThreadDescription(handle, &description)) && description && *description) {
        slot.name.clear();
        for (const wchar_t* c = description; *c && slot.name.size() < 28; ++c) {
          slot.name.push_back(*c < 128 ? static_cast<char>(*c) : '?');
        }
      }
      if (description) {
        LocalFree(description);
      }
      threads_.emplace(entry.th32ThreadID, std::move(slot));
    }
    CloseHandle(snapshot);
  }

  std::string Top(const std::unordered_map<uint64_t, uint64_t>& hits, uint64_t total, size_t count) {
    std::vector<std::pair<uint64_t, uint64_t>> sorted;
    for (const auto& [key, n] : hits) {
      sorted.emplace_back(n, key);
    }
    std::sort(sorted.rbegin(), sorted.rend());
    std::string out;
    for (size_t i = 0; i < sorted.size() && i < count; ++i) {
      out += std::format("{}{} {:.1f}%", i ? ", " : "", Name(sorted[i].second),
                         100.0 * sorted[i].first / total);
    }
    return out;
  }

  void Report() {
    std::unordered_map<uint64_t, uint64_t> all;
    uint64_t total = 0;
    std::vector<ThreadSlot*> busy;
    for (auto& [tid, slot] : threads_) {
      for (const auto& [key, n] : slot.hits) {
        all[key] += n;
      }
      total += slot.samples;
      if (slot.samples) {
        busy.push_back(&slot);
      }
    }
    if (total) {
      REXLOG_INFO("ncaabb10: profile all ({:.2f} Gcycles): {}", total * 1e-9, Top(all, total, 15));
      std::sort(busy.begin(), busy.end(), [](auto* a, auto* b) { return a->samples > b->samples; });
      for (size_t i = 0; i < busy.size() && i < 3; ++i) {
        REXLOG_INFO("ncaabb10: profile thread {} ({:.0f}% of total): {}", busy[i]->name,
                    100.0 * busy[i]->samples / total, Top(busy[i]->hits, busy[i]->samples, 10));
      }
    }
    for (auto& [tid, slot] : threads_) {
      slot.hits.clear();
      slot.samples = 0;
    }
  }

  std::vector<GuestFunction> functions_;
  uintptr_t code_begin_ = 0, code_end_ = 0;
  std::map<DWORD, ThreadSlot> threads_;
};

// Starts the sampler at static initialization when NCAABB10_PROFILE is set.
const bool g_sampler_started = [] {
  const char* value = std::getenv("NCAABB10_PROFILE");
  if (value && *value && *value != '0') {
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::seconds(3));  // Let the app set up logging.
      static Sampler sampler;
      sampler.Run();
    }).detach();
  }
  return true;
}();

}  // namespace

#endif  // _WIN32
