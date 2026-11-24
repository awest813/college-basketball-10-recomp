// ncaabb10 - calling the SDK's own version of a kernel import that the executable overrides.
//
// An override (REX_HOOK_RAW on the import name) is defined in the executable; the SDK's export
// is still in its runtime library, found here by name and called for every case the override
// does not handle itself.

#pragma once

#include <algorithm>
#include <cstdint>

#include <rex/kernel/types/status.h>
#include <rex/logging/log_category.h>
#include <rex/ppc/hook.h>
#include <rex/types.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#endif

namespace ncaabb10 {

using KernelFunc = void (*)(PPCContext&, uint8_t*);

// The export of `name` in a loaded module other than the executable, or null (logged).
inline KernelFunc Original(const char* name) {
  KernelFunc found = nullptr;
#if defined(_WIN32)
  HMODULE modules[1024];
  DWORD bytes = 0;
  if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) {
    // bytes is what all modules need, which can exceed the array.
    const DWORD count = std::min<DWORD>(bytes / sizeof(HMODULE), DWORD(std::size(modules)));
    const HMODULE exe = GetModuleHandleW(nullptr);
    for (DWORD i = 0; i < count && !found; ++i) {
      if (modules[i] != exe) {
        found = reinterpret_cast<KernelFunc>(GetProcAddress(modules[i], name));
      }
    }
  }
#else
  found = reinterpret_cast<KernelFunc>(dlsym(RTLD_NEXT, name));
#endif
  if (!found) {
    REXLOG_ERROR("ncaabb10: the SDK has no {}; calls to it return STATUS_NOT_IMPLEMENTED", name);
  }
  return found;
}

// Calls the SDK's version, or reports the call as not implemented when there is none, rather
// than jumping to null.
inline void CallOriginal(KernelFunc original, PPCContext& ctx, uint8_t* base) {
  if (original) {
    original(ctx, base);
  } else {
    ctx.r3.u64 = rex::kernel::X_STATUS_NOT_IMPLEMENTED;
  }
}

}  // namespace ncaabb10
