// ncaabb10 - kernel import fixes the SDK does not cover yet.
//
// Each override replaces an SDK export (REX_HOOK_RAW on the import name). The executable defines the
// override, so the original is still in the SDK's runtime library and is called for every case not
// handled here. Same approach as reNut's src/renut_engine/app/xam_workspace.cpp.

#include <cstdint>
#include <cstring>

#include <rex/kernel/types/io.h>
#include <rex/kernel/types/status.h>
#include <rex/ppc/hook.h>
#include <rex/types.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#endif

namespace {

using Func = void (*)(PPCContext&, uint8_t*);

// Finds the SDK's own export of `name` in a loaded module other than the executable.
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

void CallOriginal(const char* name, Func& cache, PPCContext& ctx, uint8_t* base) {
  if (!cache) {
    cache = Original(name);
  }
  if (cache) {
    cache(ctx, base);
  }
}

void StoreBE32(uint8_t* base, uint32_t address, uint32_t value) {
  const uint32_t swapped = rex::byte_swap(value);
  std::memcpy(base + address, &swapped, sizeof(swapped));
}

}  // namespace

// NtQueryInformationFile(handle, X_IO_STATUS_BLOCK* io, void* info, u32 length, u32 class)
//
// The SDK logs XFileSectorInformation as unimplemented and fails it. EA's streaming asks for the
// disc sector a file starts on (it orders reads by disc position), many times per load. Answer
// with a stable pseudo sector per open file and success, as Xenia and the Skate 3 SDK fork do
// (they hash the path; the handle is enough for ordering reads within a session).
REX_HOOK_RAW(__imp__NtQueryInformationFile) {
  static Func original = nullptr;
  const uint32_t handle = ctx.r3.u32;
  const uint32_t io_status = ctx.r4.u32;
  const uint32_t info = ctx.r5.u32;
  const uint32_t length = ctx.r6.u32;
  const uint32_t info_class = ctx.r7.u32;
  if (info_class != rex::kernel::XFileSectorInformation) {
    return CallOriginal("__imp__NtQueryInformationFile", original, ctx, base);
  }
  if (length < 4 || !info) {
    ctx.r3.u64 = rex::kernel::X_STATUS_INFO_LENGTH_MISMATCH;
    return;
  }
  uint32_t sector = handle * 2654435761u;  // Knuth multiplicative hash: spread, never 0 for h != 0.
  sector = (sector ^ (sector >> 15)) & 0x00FFFFFF;  // Keep it within a DVD's sector range.
  std::memset(base + info, 0, length);
  StoreBE32(base, info, sector);
  if (io_status) {
    StoreBE32(base, io_status, rex::kernel::X_STATUS_SUCCESS);
    StoreBE32(base, io_status + 4, 4);
  }
  ctx.r3.u64 = rex::kernel::X_STATUS_SUCCESS;
}
