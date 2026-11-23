// ncaabb10 - diagnostics for the dirty-disc freeze (XamShowDirtyDiscErrorUI on the court loading
// screen in some boots). Logs only; every hook calls the original.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rex/logging/log_category.h>
#include <rex/ppc/hook.h>
#include <rex/types.h>

#include "perf_counters.h"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#endif

REX_EXTERN(__imp__sub_826C0388);

namespace {

using Func = void (*)(PPCContext&, uint8_t*);

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

uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, sizeof(value));
  return rex::byte_swap(value);
}

// A guest C string, printable characters only, at most `limit` bytes.
std::string GuestString(const uint8_t* base, uint32_t address, size_t limit = 160) {
  std::string out;
  if (address < 0x10000) {
    return "<null>";
  }
  for (size_t i = 0; i < limit; ++i) {
    char c = static_cast<char>(base[address + i]);
    if (!c) {
      break;
    }
    out.push_back(c >= 32 && c < 127 ? c : '?');
  }
  return out;
}

// Return addresses up the guest stack. Functions save LR at (entry r1 - 8) and store the back
// chain at 0(r1), so the LR of the function owning a frame is at (back chain - 8).
std::string GuestBacktrace(const uint8_t* base, uint32_t sp, uint32_t lr) {
  std::string out = std::format("{:08X}", lr);
  for (int depth = 0; depth < 16; ++depth) {
    if (sp < 0x10000 || (sp & 3)) {
      break;
    }
    uint32_t back = Load32(base, sp);
    if (back <= sp || back - sp > 0x100000) {
      break;
    }
    uint32_t saved_lr = Load32(base, back - 8);
    if (saved_lr < 0x82000000 || saved_lr >= 0x84000000) {
      break;
    }
    out += std::format(" <- {:08X}", saved_lr);
    sp = back;
  }
  return out;
}

}  // namespace

// The game's error callback that raises the dirty-disc UI unless the message contains one of
// two strings (checked with sub_82D65160, probably strstr). No direct callers: it is reached
// through a function pointer.
REX_HOOK_RAW(sub_826C0388) {
  REXLOG_WARN("ncaabb10: error callback r3={:08X} r4={:08X} r5={:08X} msg='{}' accept=['{}', '{}']",
              ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, GuestString(base, ctx.r5.u32),
              GuestString(base, 0x82136204), GuestString(base, 0x8213620C));
  REXLOG_WARN("ncaabb10: error callback stack {}", GuestBacktrace(base, ctx.r1.u32, ctx.lr));
  __imp__sub_826C0388(ctx, base);
}

// Reads that fail or take long. NtReadFile(handle, event, apc, apc_ctx, io_status, buffer,
// length, offset*); NtReadFileScatter has the same first five, then segments, length, offset*.
namespace {
std::atomic<uint64_t> g_reads{0};

// Calls an SDK kernel export on a copy of the context with r3 = arg and returns r3.
uint32_t CallExport(const char* name, Func& cache, const PPCContext& ctx, uint8_t* base, uint32_t arg) {
  if (!cache) {
    cache = Original(name);
  }
  if (!cache) {
    return 0xFFFFFFFF;
  }
  PPCContext copy = ctx;
  copy.r3.u64 = arg;
  cache(copy, base);
  return copy.r3.u32;
}

// Logs the guest protection of every 4 KiB page of a range, as runs, and the allocation size
// the kernel reports at the start and end of the range.
void DumpPages(const PPCContext& ctx, uint8_t* base, uint32_t buffer, uint32_t length) {
  static Func query_protect = nullptr, query_size = nullptr;
  const uint32_t first = buffer & ~0xFFFu, last = (buffer + length - 1) & ~0xFFFu;
  std::string runs;
  uint32_t run_start = first, run_protect = 0;
  for (uint32_t page = first;; page += 0x1000) {
    const uint32_t protect =
        CallExport("__imp__MmQueryAddressProtect", query_protect, ctx, base, page);
    if (page == first) {
      run_protect = protect;
    } else if (protect != run_protect) {
      runs += std::format(" [{:08X}-{:08X} {:#x}]", run_start, page - 1, run_protect);
      run_start = page;
      run_protect = protect;
    }
    if (page == last) {
      runs += std::format(" [{:08X}-{:08X} {:#x}]", run_start, page + 0xFFF, run_protect);
      break;
    }
  }
  REXLOG_WARN("ncaabb10: failed read pages{} alloc_size(start)={:#x} alloc_size(end)={:#x}", runs,
              CallExport("__imp__MmQueryAllocationSize", query_size, ctx, base, buffer),
              CallExport("__imp__MmQueryAllocationSize", query_size, ctx, base, buffer + length - 1));
}

void TimedRead(const char* name, Func& original, PPCContext& ctx, uint8_t* base) {
  if (!original) {
    original = Original(name);
    REXLOG_INFO("ncaabb10: {} hook active, original {}", name + 7, original ? "found" : "MISSING");
  }
  ++g_reads;
  const uint32_t handle = ctx.r3.u32, io_status = ctx.r7.u32, buffer = ctx.r8.u32,
                 length = ctx.r9.u32, offset_ptr = ctx.r10.u32;
  const uint64_t offset =
      offset_ptr ? (uint64_t(Load32(base, offset_ptr)) << 32) | Load32(base, offset_ptr + 4) : ~0ull;
  const uint32_t caller_sp = ctx.r1.u32, caller_lr = ctx.lr;
  const auto start = std::chrono::steady_clock::now();
  original(ctx, base);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  const uint32_t status = ctx.r3.u32;
  const uint32_t io_result = io_status ? Load32(base, io_status) : 0;
  const uint32_t io_info = io_status ? Load32(base, io_status + 4) : 0;
  auto& counters = ncaabb10::perf::Get();
  counters.reads.fetch_add(1, std::memory_order_relaxed);
  counters.read_bytes.fetch_add(io_status && status == 0 ? io_info : 0, std::memory_order_relaxed);
  counters.read_micros.fetch_add(static_cast<uint64_t>(ms * 1000.0), std::memory_order_relaxed);
  const bool failed = (status != 0 && status != 0x103) || (io_status && io_result != 0 && io_result != 0x103);
  if (failed || ms > 50.0) {
    REXLOG_WARN("ncaabb10: {} h={:08X} buf={:08X} len={:#x} off={:#x} -> {:08X} io=({:08X}, {:#x}) {:.1f} ms",
                name + 7, handle, buffer, length, offset, status, io_result, io_info, ms);
  }
  if (status == 0xC0000005 && buffer && length) {
    REXLOG_WARN("ncaabb10: failed read stack {}", GuestBacktrace(base, caller_sp, caller_lr));
    DumpPages(ctx, base, buffer, length);
  }
}
}  // namespace

// Physical allocations and frees, to see what owned a failing read's buffer. About 2000 lines a
// game, so only with NCAABB10_TRACE_PHYS set. This tracing found the PhysicalHeap release race
// (patches/rexglue-sdk-0.11.0-physical-heap-release-race.patch).
namespace {
bool TracePhysical() {
  static const bool enabled = [] {
    const char* value = std::getenv("NCAABB10_TRACE_PHYS");
    return value && *value && *value != '0';
  }();
  return enabled;
}
}  // namespace

// MmAllocatePhysicalMemoryEx(flags, size, protect, min, max, alignment) -> address.
REX_HOOK_RAW(__imp__MmAllocatePhysicalMemoryEx) {
  static Func original = nullptr;
  if (!original) {
    original = Original("__imp__MmAllocatePhysicalMemoryEx");
  }
  const uint32_t size = ctx.r4.u32, protect = ctx.r5.u32, alignment = ctx.r8.u32;
  const uint32_t caller_lr = ctx.lr;
  original(ctx, base);
  if (TracePhysical()) {
    REXLOG_INFO("ncaabb10: phys alloc size={:#x} prot={:#x} align={:#x} -> {:08X} (lr {:08X})",
                size, protect, alignment, ctx.r3.u32, caller_lr);
  }
}

// MmFreePhysicalMemory(type, address).
REX_HOOK_RAW(__imp__MmFreePhysicalMemory) {
  static Func original = nullptr;
  if (!original) {
    original = Original("__imp__MmFreePhysicalMemory");
  }
  if (TracePhysical()) {
    REXLOG_INFO("ncaabb10: phys free {:08X} type={} (lr {:08X})", ctx.r4.u32, ctx.r3.u32, ctx.lr);
  }
  original(ctx, base);
}

REX_HOOK_RAW(__imp__NtReadFile) {
  static Func original = nullptr;
  TimedRead("__imp__NtReadFile", original, ctx, base);
}

REX_HOOK_RAW(__imp__NtReadFileScatter) {
  static Func original = nullptr;
  TimedRead("__imp__NtReadFileScatter", original, ctx, base);
}
