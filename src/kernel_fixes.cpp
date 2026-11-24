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

#include "sdk_original.h"

namespace {

using Func = ncaabb10::KernelFunc;
using ncaabb10::CallOriginal;
using ncaabb10::Original;

void StoreBE32(uint8_t* base, uint32_t address, uint32_t value) {
  const uint32_t swapped = rex::byte_swap(value);
  std::memcpy(base + address, &swapped, sizeof(swapped));
}

}  // namespace

// NtQueryInformationFile(handle, X_IO_STATUS_BLOCK* io, void* info, u32 length, u32 class)
//
// EA's streaming asks for the disc sector a file starts on (it orders reads by disc position),
// many times per load. SDK 0.11.0 answers XFileSectorInformation through a stub that logs a
// warning on every call and returns a full 32-bit path hash. Answer here with a stable pseudo
// sector per open file and no log (as Xenia and the Skate 3 SDK
// fork do; they hash the path, the handle is enough for ordering reads within a session).
REX_HOOK_RAW(__imp__NtQueryInformationFile) {
  static const Func original = Original("__imp__NtQueryInformationFile");
  const uint32_t handle = ctx.r3.u32;
  const uint32_t io_status = ctx.r4.u32;
  const uint32_t info = ctx.r5.u32;
  const uint32_t length = ctx.r6.u32;
  const uint32_t info_class = ctx.r7.u32;
  if (info_class != rex::kernel::XFileSectorInformation) {
    return CallOriginal(original, ctx, base);
  }
  if (length < 4 || !info) {
    ctx.r3.u64 = rex::kernel::X_STATUS_INFO_LENGTH_MISMATCH;
    return;
  }
  // Knuth multiplicative hash: spreads handles, which are close together. The game only orders
  // reads by the value, so 24 bits (a disc's sectors are under 2^22) leave it room without ever
  // looking like a byte offset; it can be 0, as a file at the start of the disc would be.
  uint32_t sector = handle * 2654435761u;
  sector = (sector ^ (sector >> 15)) & 0x00FFFFFF;
  std::memset(base + info, 0, length);
  StoreBE32(base, info, sector);
  if (io_status) {
    StoreBE32(base, io_status, rex::kernel::X_STATUS_SUCCESS);
    StoreBE32(base, io_status + 4, 4);
  }
  ctx.r3.u64 = rex::kernel::X_STATUS_SUCCESS;
}
