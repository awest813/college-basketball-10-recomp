// ncaabb10 - diagnostics for the dirty-disc freeze (XamShowDirtyDiscErrorUI on the court loading
// screen in some boots). Logs only, and every hook calls the original, except that
// MmFreePhysicalMemory skips a free of memory that is not allocated (see there).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rex/logging/log_category.h>
#include <rex/ppc/hook.h>
#include <rex/types.h>

#include "perf_counters.h"

#include "sdk_original.h"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#endif

REX_EXTERN(__imp__sub_826C0388);

namespace {

using Func = ncaabb10::KernelFunc;
using ncaabb10::CallOriginal;
using ncaabb10::Original;

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
  // Not past the end of the 4 GB guest space.
  limit = std::min<uint64_t>(limit, 0x100000000ull - address);
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

// Calls an SDK kernel export with r3 = arg on the thread's own context (the SDK checks that an
// export runs on it) and returns its r3, leaving r3 as it was.
uint32_t CallExport(Func function, PPCContext& ctx, uint8_t* base, uint32_t arg) {
  if (!function) {
    return 0xFFFFFFFF;
  }
  const uint64_t saved_r3 = ctx.r3.u64;
  ctx.r3.u64 = arg;
  function(ctx, base);
  const uint32_t result = ctx.r3.u32;
  ctx.r3.u64 = saved_r3;
  return result;
}

// Logs the guest protection of every 4 KiB page of a range, as runs, and the allocation size
// the kernel reports at the start and end of the range.
void DumpPages(PPCContext& ctx, uint8_t* base, uint32_t buffer, uint32_t length) {
  static const Func query_protect = Original("__imp__MmQueryAddressProtect");
  static const Func query_size = Original("__imp__MmQueryAllocationSize");
  // Clamped to the guest space and to 64 MB of pages: a bad length must not wrap or run on.
  const uint64_t end = std::min<uint64_t>({uint64_t(buffer) + length - 1, 0xFFFFFFFFull,
                                          uint64_t(buffer) + (64u << 20) - 1});
  const uint32_t first = buffer & ~0xFFFu, last = static_cast<uint32_t>(end) & ~0xFFFu;
  std::string runs;
  uint32_t run_start = first, run_protect = 0;
  for (uint32_t page = first;; page += 0x1000) {
    const uint32_t protect =
        CallExport(query_protect, ctx, base, page);
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
              CallExport(query_size, ctx, base, buffer),
              CallExport(query_size, ctx, base, static_cast<uint32_t>(end)));
}

// The SDK export a read hook forwards to, logged once.
Func ReadOriginal(const char* name) {
  const Func original = Original(name);
  REXLOG_INFO("ncaabb10: {} hook active, original {}", name + 7, original ? "found" : "MISSING");
  return original;
}

// NtReadFileScatter's r8 is its segment array, not a buffer; only NtReadFile's buffer is dumped.
void TimedRead(const char* name, Func original, bool scatter, PPCContext& ctx, uint8_t* base) {
  const uint32_t handle = ctx.r3.u32, io_status = ctx.r7.u32, buffer = ctx.r8.u32,
                 length = ctx.r9.u32, offset_ptr = ctx.r10.u32;
  const uint64_t offset =
      offset_ptr ? (uint64_t(Load32(base, offset_ptr)) << 32) | Load32(base, offset_ptr + 4) : ~0ull;
  const uint32_t caller_sp = ctx.r1.u32, caller_lr = ctx.lr;
  const auto start = std::chrono::steady_clock::now();
  CallOriginal(original, ctx, base);
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
  if (status == 0xC0000005 && buffer && length && !scatter) {
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
  static const Func original = Original("__imp__MmAllocatePhysicalMemoryEx");
  const uint32_t size = ctx.r4.u32, protect = ctx.r5.u32, alignment = ctx.r8.u32;
  const uint32_t caller_lr = ctx.lr;
  CallOriginal(original, ctx, base);
  if (TracePhysical()) {
    REXLOG_INFO("ncaabb10: phys alloc size={:#x} prot={:#x} align={:#x} -> {:08X} (lr {:08X})",
                size, protect, alignment, ctx.r3.u32, caller_lr);
  }
}

// MmFreePhysicalMemory(type, address). Returns nothing.
//
// Each game load frees six small buffers twice in a row (from 826C066C). The SDK refuses the
// second free without changing anything, but logs it as two errors. An address with no allocation
// (MmQueryAllocationSize 0, as after the first free) is skipped here and logged at debug level,
// which is the same outcome without the errors; any other free goes to the SDK as before.
REX_HOOK_RAW(__imp__MmFreePhysicalMemory) {
  static const Func original = Original("__imp__MmFreePhysicalMemory");
  static const Func query_size = Original("__imp__MmQueryAllocationSize");
  const uint32_t address = ctx.r4.u32;
  if (TracePhysical()) {
    REXLOG_INFO("ncaabb10: phys free {:08X} type={} (lr {:08X})", address, ctx.r3.u32, ctx.lr);
  }
  if (query_size && address && CallExport(query_size, ctx, base, address) == 0) {
    REXLOG_DEBUG("ncaabb10: phys free {:08X} again (not allocated), skipped (lr {:08X})", address,
                 ctx.lr);
    return;
  }
  CallOriginal(original, ctx, base);
}

REX_HOOK_RAW(__imp__NtReadFile) {
  static const Func original = ReadOriginal("__imp__NtReadFile");
  TimedRead("__imp__NtReadFile", original, false, ctx, base);
}

REX_HOOK_RAW(__imp__NtReadFileScatter) {
  static const Func original = ReadOriginal("__imp__NtReadFileScatter");
  TimedRead("__imp__NtReadFileScatter", original, true, ctx, base);
}
