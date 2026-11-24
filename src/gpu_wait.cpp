// ncaabb10 - a cheaper GPU wait for the title's render thread.
//
// The XDK Direct3D code waits for the GPU (ring buffer read pointer reaching a fence) in
// sub_823E4050, which calls sub_823E1148 in a loop. sub_823E1148 lowers the hardware thread
// priority, runs 32 `db16cyc` delay hints, restores priority and checks the device. The hints and
// priority changes compile to nothing, so the loop is a tight spin, and the render thread spends
// most of each frame in it: a whole host core busy doing nothing.
//
// This hook keeps the original function for every check (fence, device state, the hang timeout)
// and adds the delay the hints stood for: during the first kSpinMicroseconds of a wait it spins with
// a pause hint, after that each poll sleeps kSleepMicroseconds on a high resolution timer. Set
// NCAABB10_GPU_WAIT_SPIN=1 for the original spin.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include <rex/ppc/hook.h>

#include "perf_counters.h"

#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

REX_EXTERN(__imp__sub_823E1148);

namespace {

using Clock = std::chrono::steady_clock;

constexpr int64_t kSpinMicroseconds = 50;
constexpr int64_t kSleepMicroseconds = 100;
// Polls closer together than this belong to one wait.
constexpr int64_t kSameWaitMicroseconds = 200;

bool KeepSpin() {
  static const bool keep = [] {
    const char* value = std::getenv("NCAABB10_GPU_WAIT_SPIN");
    return value && *value && *value != '0';
  }();
  return keep;
}

void CpuPause() {
#if defined(__x86_64__) || defined(_M_X64)
  _mm_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield");
#endif
}

void SleepMicroseconds(int64_t microseconds) {
#if defined(_WIN32)
  // One timer per thread, closed when the thread ends (the render thread restarts with the title).
  struct Timer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                           TIMER_ALL_ACCESS);
    ~Timer() {
      if (handle) {
        CloseHandle(handle);
      }
    }
  };
  thread_local Timer owner;
  const HANDLE timer = owner.handle;
  if (timer) {
    LARGE_INTEGER due;
    due.QuadPart = -microseconds * 10;  // Relative, in 100 ns units.
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
      WaitForSingleObject(timer, INFINITE);
      return;
    }
  }
#endif
  std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

struct WaitState {
  Clock::time_point wait_start{};
  Clock::time_point last_poll{};
};

}  // namespace

REX_HOOK_RAW(sub_823E1148) {
  if (!KeepSpin()) {
    thread_local WaitState state;
    const auto now = Clock::now();
    if (now - state.last_poll > std::chrono::microseconds(kSameWaitMicroseconds)) {
      state.wait_start = now;  // A new wait.
    }
    if (now - state.wait_start < std::chrono::microseconds(kSpinMicroseconds)) {
      for (int i = 0; i < 32; ++i) {
        CpuPause();
      }
    } else {
      SleepMicroseconds(kSleepMicroseconds);
      ncaabb10::perf::Get().gpu_waits.fetch_add(1, std::memory_order_relaxed);
    }
    state.last_poll = Clock::now();
  }
  __imp__sub_823E1148(ctx, base);
}
