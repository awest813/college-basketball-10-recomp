// ncaabb10 - records the title's mixed audio. With NCAABB10_AUDIO_DUMP=<file.wav>, every frame the
// title submits through XAudioSubmitRenderDriverFrame (256 samples x 6 channels, big endian float,
// one block per channel) is appended to a 48 kHz 6-channel 32-bit float WAV. The header is rewritten
// every second so the file stays valid when the process is killed.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include <rex/logging/log_category.h>
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

constexpr int kChannels = 6;
constexpr int kFrameSamples = 256;
constexpr int kSampleRate = 48000;
constexpr uint64_t kMaxDataBytes = 0xFFFFFFFFull - 36;

class WavWriter {
 public:
  explicit WavWriter(const char* path) : file_(std::fopen(path, "wb")) {
    if (file_) {
      WriteHeader();
    }
  }

  void Append(const float* interleaved, size_t samples) {
    std::lock_guard lock(mutex_);
    if (!file_) {
      return;
    }
    const uint64_t bytes = samples * sizeof(float);
    if (data_bytes_ + bytes > kMaxDataBytes) {  // A WAV's sizes are 32-bit: stop at about 4 GiB.
      WriteHeader();
      std::fclose(file_);
      file_ = nullptr;
      REXLOG_INFO("ncaabb10: audio capture stopped at the WAV size limit");
      return;
    }
    std::fwrite(interleaved, sizeof(float), samples, file_);
    data_bytes_ += static_cast<uint32_t>(bytes);
    if (++frames_ % (kSampleRate / kFrameSamples) == 0) {  // About once a second.
      WriteHeader();
      std::fseek(file_, 0, SEEK_END);  // Not ftell: a long is 32-bit on Windows.
      std::fflush(file_);
    }
  }

 private:
  void WriteHeader() {
    auto u32 = [this](uint32_t v) { std::fwrite(&v, 4, 1, file_); };
    auto u16 = [this](uint16_t v) { std::fwrite(&v, 2, 1, file_); };
    std::fseek(file_, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, file_);
    u32(36 + data_bytes_);
    std::fwrite("WAVEfmt ", 1, 8, file_);
    u32(16);
    u16(3);  // IEEE float
    u16(kChannels);
    u32(kSampleRate);
    u32(kSampleRate * kChannels * 4);
    u16(kChannels * 4);
    u16(32);
    std::fwrite("data", 1, 4, file_);
    u32(data_bytes_);
  }

  std::FILE* file_;
  std::mutex mutex_;
  uint32_t data_bytes_ = 0;
  uint64_t frames_ = 0;
};

WavWriter* Writer() {
  static WavWriter* writer = [] () -> WavWriter* {
    const char* path = std::getenv("NCAABB10_AUDIO_DUMP");
    return path && *path ? new WavWriter(path) : nullptr;
  }();
  return writer;
}

}  // namespace

// XAudioSubmitRenderDriverFrame(driver, samples)
REX_HOOK_RAW(__imp__XAudioSubmitRenderDriverFrame) {
  static const Func original = Original("__imp__XAudioSubmitRenderDriverFrame");
  if (WavWriter* writer = Writer(); writer && ctx.r4.u32) {
    float interleaved[kFrameSamples * kChannels];
    const uint8_t* planar = base + ctx.r4.u32;
    for (int c = 0; c < kChannels; ++c) {
      for (int i = 0; i < kFrameSamples; ++i) {
        uint32_t raw;
        std::memcpy(&raw, planar + (c * kFrameSamples + i) * 4, 4);
        raw = rex::byte_swap(raw);
        std::memcpy(&interleaved[i * kChannels + c], &raw, 4);
      }
    }
    writer->Append(interleaved, kFrameSamples * kChannels);
  }
  original(ctx, base);
}
