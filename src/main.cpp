// ncaabb10 - ReXGlue Recompiled Project

#include <SDL3/SDL.h>

#include <rex/app/title_app.h>
#include <rex/filesystem/file_system.h>
#include <rex/hid/input_system.h>
#include <rex/input_keys.h>
#include <rex/kernel/title_runtime.h>
#include <rex/logging/log_category.h>
#include <rex/path_settings.h>
#include <rex/platform/file.h>
#include <rex/setting.h>
#include <rex/settings_registry.h>
#include <rex/string/text.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "generated/default/ncaabb10_init.h"
#include "frontend.h"
#include "ncaabb10_git.h"  // NCAABB10_GIT, written by the build (cmake/git_rev.cmake)

namespace {

// The game mounts the console's utility (cache) partition through the XDK's
// XMountUtilityDrive and opens "cache:\". Nothing in the standard mounts covers it, so the
// game retried three times and went on without it. Back it with a writable host folder under
// the user data root, the way Xenia does (\CACHE plus a cache: link).
rex::Result<void> MountUtilityDrive(rex::kernel::TitleRuntime& title) {
  namespace fs = rex::filesystem;
  const std::filesystem::path host = REX_PATHS().user_root.Get() / "cache";
  std::error_code ec;
  std::filesystem::create_directories(host, ec);
  if (ec) {
    REXLOG_ERROR("ncaabb10: cannot create {}: {}", host.string(), ec.message());
  }
  auto& files = title.GetFileSystem();
  fs::MountEntry entry;
  entry.path = "\\CACHE";
  entry.type = fs::ProviderType::kHostDirectory;
  entry.source = host;
  entry.flags = fs::MountFlags::kWritable;
  REX_TRY_VOID(files.Mount(entry));
  REX_TRY_VOID(files.Link("cache:", "\\CACHE"));
  REXLOG_INFO("ncaabb10: utility drive cache: -> {}", host.string());
  return rex::Ok();
}

// Pushes a key press and release into the SDL event queue of the game window.
void PushWindowKey(unsigned vk, bool alt) {
  SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
  if (vk >= 0x70 && vk <= 0x7B) {
    scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (vk - 0x70));  // F1-F12
  } else if (vk == 0x0D) {
    scancode = SDL_SCANCODE_RETURN;
  } else if (vk == 0x1B) {
    scancode = SDL_SCANCODE_ESCAPE;
  } else if (vk >= 'A' && vk <= 'Z') {
    scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (vk - 'A'));
  } else if (vk == 0x20) {
    scancode = SDL_SCANCODE_SPACE;
  } else if (vk == 0x09) {
    scancode = SDL_SCANCODE_TAB;
  } else if (vk >= 0x25 && vk <= 0x28) {  // Left, up, right, down.
    static constexpr SDL_Scancode kArrows[] = {SDL_SCANCODE_LEFT, SDL_SCANCODE_UP,
                                               SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN};
    scancode = kArrows[vk - 0x25];
  }
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  if (scancode == SDL_SCANCODE_UNKNOWN || !windows || count == 0) {
    SDL_free(windows);
    return;
  }
  const SDL_WindowID window = SDL_GetWindowID(windows[0]);
  SDL_free(windows);
  const SDL_Keymod mod = alt ? SDL_KMOD_LALT : SDL_KMOD_NONE;
  auto push = [&](SDL_Scancode code, bool down, SDL_Keymod with) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = window;
    event.key.scancode = code;
    event.key.key = SDL_GetKeyFromScancode(code, with, false);
    event.key.mod = with;
    event.key.down = down;
    SDL_PushEvent(&event);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  };
  // A held Alt goes down before the key and up after it, as on a keyboard.
  if (alt) {
    push(SDL_SCANCODE_LALT, true, mod);
  }
  push(scancode, true, mod);
  push(scancode, false, mod);
  if (alt) {
    push(SDL_SCANCODE_LALT, false, SDL_KMOD_NONE);
  }
}

// Virtual controllers for test scripts, attached on first use. SDL lists them like real pads, so
// they go through the SDK's SDL input driver to the title and through the front end's menus.
// The title's vibration requests reach them too and are logged.
constexpr int kTestPads = 2;
SDL_Joystick* g_test_pads[kTestPads] = {};

bool SDLCALL TestPadRumble(void* userdata, Uint16 low, Uint16 high) {
  static std::atomic<uint32_t> last[kTestPads] = {};
  const int slot = static_cast<int>(reinterpret_cast<intptr_t>(userdata));
  const uint32_t now = (uint32_t{low} << 16) | high;
  if (last[slot].exchange(now) != now) {
    REXLOG_INFO("ncaabb10: test controller {} rumble low {} high {}", slot, low, high);
  }
  return true;
}

SDL_Joystick* TestPad(int slot) {
  if (g_test_pads[slot]) {
    return g_test_pads[slot];
  }
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
  desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
  desc.name = slot == 0 ? "Test controller" : "Test controller 2";
  desc.userdata = reinterpret_cast<void*>(static_cast<intptr_t>(slot));
  desc.Rumble = &TestPadRumble;
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  g_test_pads[slot] = id ? SDL_OpenJoystick(id) : nullptr;
  REXLOG_INFO("ncaabb10: test controller {} attached: {}", slot,
              g_test_pads[slot] ? "yes" : SDL_GetError());
  // Let SDL and the input driver list it before its first press.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  return g_test_pads[slot];
}

// Unplugs a test controller, as pulling its cable would.
void UnplugTestPad(int slot) {
  if (SDL_Joystick* pad = g_test_pads[slot]) {
    const SDL_JoystickID id = SDL_GetJoystickID(pad);
    SDL_CloseJoystick(pad);
    SDL_DetachVirtualJoystick(id);
    g_test_pads[slot] = nullptr;
    REXLOG_INFO("ncaabb10: test controller {} unplugged", slot);
  }
}

// Development aid for automated testing. When NCAABB10_INPUT_FILE names a file, a thread follows it
// and feeds each new line to the input system as a key event, with input capture forced on, so a
// test script can drive the game without touching the desktop's real keyboard focus. Lines:
//   down <vk>   key down (Windows virtual key code, decimal)
//   up <vk>     key up
//   ui <vk> [alt]   a key press through the window's event queue
//   pad <button> <0|1> [slot]   a test controller button (SDL_GamepadButton number) up or down
//   axis <axis> <value> [slot]  a test controller axis (SDL_GamepadAxis number), -32768 to 32767
//   unplug <slot>               unplugs a test controller; its next pad or axis line plugs it in
// Slot is 0 (the default) or 1, a second controller.
// With hid.mnk_mode on (the default) the keys act as pad buttons through hid.keybind_*.
// SDL ignores controllers while the window is in the background, so a script that drives the test
// controller sets SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1.
// The returned thread stops and joins when destroyed, which must happen before the input system
// and SDL shut down.
std::jthread StartInputFeed(rex::hid::IInputSystem& input) {
  const char* file = std::getenv("NCAABB10_INPUT_FILE");
  if (!file || !*file) {
    return {};
  }
  std::string path = file;
  REXLOG_INFO("ncaabb10: following input file {}", path);
  return std::jthread([&input, path](std::stop_token stop) {
    // Open, read the complete new lines and close on every poll: an open ifstream would keep
    // the writer from appending on Windows.
    std::streamoff consumed = 0;
    std::vector<std::string> lines;
    while (!stop.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      lines.clear();
      {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
          continue;
        }
        // A file cut shorter than what was read (a test script that starts it over) starts over.
        in.seekg(0, std::ios::end);
        if (in.tellg() < consumed) {
          consumed = 0;
        }
        in.seekg(consumed);
        std::string line;
        while (std::getline(in, line) && !in.eof()) {  // Skip a partly written last line.
          consumed = in.tellg();
          lines.push_back(line);
        }
      }
      for (const auto& line : lines) {
        if (stop.stop_requested()) {
          break;
        }
        std::istringstream words(line);
        std::string verb;
        if (line.starts_with("text ")) {
          // Typed text, as the window's text input would deliver it (ImGui text fields). The event
          // points at its string, so keep each one alive.
          static std::vector<std::unique_ptr<std::string>> texts;
          texts.push_back(std::make_unique<std::string>(line.substr(5)));
          SDL_Event event{};
          event.type = SDL_EVENT_TEXT_INPUT;
          int count = 0;
          if (SDL_Window** windows = SDL_GetWindows(&count)) {
            event.text.windowID = count > 0 ? SDL_GetWindowID(windows[0]) : 0;
            SDL_free(windows);
          }
          event.text.text = texts.back()->c_str();
          SDL_PushEvent(&event);
          REXLOG_INFO("ncaabb10: input feed text '{}'", *texts.back());
          continue;
        }
        if (line.starts_with("unplug ")) {
          int slot = 0;
          if (words >> verb >> slot && slot >= 0 && slot < kTestPads) {
            UnplugTestPad(slot);
          }
          continue;
        }
        if (line.starts_with("pad ") || line.starts_with("axis ")) {
          int index = 0, value = 0, slot = 0;
          if (!(words >> verb >> index >> value)) {
            continue;
          }
          words >> slot;
          if (slot < 0 || slot >= kTestPads) {
            continue;
          }
          if (SDL_Joystick* pad = TestPad(slot)) {
            if (verb == "pad") {
              SDL_SetJoystickVirtualButton(pad, index, value != 0);
            } else {
              SDL_SetJoystickVirtualAxis(pad, index, static_cast<Sint16>(value));
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(60));
          auto state = input.GetState(static_cast<uint32_t>(slot));
          REXLOG_INFO("ncaabb10: input feed {} {} {} slot {} -> user {} buttons {:#06x} lx {} ly {} lt {}",
                      verb, index, value, slot, slot, state ? state->state.buttons : 0xFFFF,
                      state ? state->state.thumb_lx : 0, state ? state->state.thumb_ly : 0,
                      state ? state->state.left_trigger : 0);
          continue;
        }
        unsigned vk = 0;
        if (!(words >> verb >> vk)) {
          continue;
        }
        if (verb == "ui") {
          // A key press through the window's own event queue, as a real key would arrive: it
          // reaches the UI binds (F1, F11, Alt+Enter) and ImGui, not only the title's pad.
          std::string modifier;
          words >> modifier;
          PushWindowKey(vk, modifier == "alt");
          REXLOG_INFO("ncaabb10: input feed ui {} {}", vk, modifier);
          continue;
        }
        if (verb != "down" && verb != "up") {
          continue;
        }
        rex::InputEvent focus;  // The window may not have real focus while a script drives it.
        focus.type = rex::InputEvent::Type::kFocusGained;
        input.OnInputEvent(focus);
        input.SetInputCaptured(true);
        rex::InputEvent event;
        event.type =
            verb == "down" ? rex::InputEvent::Type::kKeyDown : rex::InputEvent::Type::kKeyUp;
        event.key = static_cast<rex::VirtualKey>(vk);
        input.OnInputEvent(event);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        auto pad = input.GetState(0);
        REXLOG_INFO("ncaabb10: input feed {} {} -> key down {}, mnk {}, user 0 buttons {:#06x}",
                    verb, vk, input.IsKeyDown(event.key), input.GetSettings().mnk_mode.Get(),
                    pad ? pad->state.buttons : 0xFFFF);
      }
    }
  });
}

}  // namespace

class Ncaabb10App : public rex::app::TitleApp {
 public:
  Ncaabb10App() : TitleApp("ncaabb10", kPPCImageConfig) {}

 protected:
  // Project defaults, at the title layer: the config file, REX_* environment variables and the
  // command line still override them.
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(TitleApp::OnConfigure(settings));
    ncaabb10::frontend::Configure(settings);
    struct Default {
      const char* name;
      const char* value;
    };
    static constexpr Default kDefaults[] = {
        // The SDK picks the ROV render target path on Intel GPUs. With it this game runs at
        // 12-20 fps in game on an Arc A750 (GPU 3D engine 93% busy); with host render targets
        // (RTV) it holds about 60 fps and renders the same.
        {"gpu.d3d12.render_target_path", "rtv"},
        // The SDK forces 4x anisotropic filtering on every mipmapped texture. The title's own
        // filtering is what the console did and costs less GPU time; live play holds 60 fps
        // (p95 17.6 ms) with it. Raise it for sharper floors at grazing angles.
        {"gpu.anisotropic_override", "-1"},
        // The title paces itself on the SDK's emulated 60 Hz vblank. Waiting for the display's
        // vsync as well ties the command processor to a second 60 Hz clock; when the two drift
        // into phase a present blocks and the title misses a vblank (live play: 12 of 24 windows
        // at a clean 60 with vsync, 22 of 24 without). Present without waiting, and without
        // tearing: the flip model swap chain shows the newest frame on the display's refresh.
        {"gpu.vsync", "false"},
        // The title renders 16:9; keep that shape on 16:10 and ultrawide displays.
        {"ui.aspect_ratio", "16:9"},
        {"gpu.d3d12.allow_tearing", "false"},
        // The practice court behind the main menu reinterprets its depth buffer as a 640 wide 4x
        // MSAA target with an unclipped rectangle and an 8192 scissor. Without running that
        // draw's vertex shader on the CPU the SDK sizes the target to all of EDRAM; it wraps over
        // the color buffer, and every frame without light flares after it presents black (5-10 s
        // at a time). Xenia runs it on the CPU by default; only such unclipped draws pay for it.
        {"gpu.xenos.execute_unclipped_draw_vs_on_cpu", "true"},
        // The keyboard is player 1's controller too, so the game is playable without a pad. A
        // connected pad still works: the keyboard merges into user 0 with the first pad.
        {"hid.mnk_mode", "true"},
        // Compiled shaders and pipelines kept on disk (cache/shaders beside user/). The SDK's
        // default is no cache folder, which stores nothing: every run compiled every shader again
        // as it first appeared, with hitches up to 160 ms in the first minutes of a game.
        {"app.cache_root", "{app.root}/cache"},
        // Each start writes logs/ncaabb10_<run>.log, and the SDK keeps every one by default (a
        // tester's folder reached 320 files). Keep the newest 50 MB of older runs.
        {"log.folder_budget", "52428800"},
        // The window and taskbar name. Empty, the SDK shows the app name and its own build
        // ("ncaabb10 [rexglue-v0.11.0...-Release]"); the log's first lines carry that.
        {"ui.title", "NCAA Basketball 10"},
        // The SDK never hides an idle mouse cursor, so one stayed over the game in fullscreen.
        // Moving the mouse shows it again.
        {"ui.cursor_timeout", "3"},
    };
    for (const Default& d : kDefaults) {
      if (auto set = settings.SetText(rex::SettingLayer::kTitle, d.name, d.value); !set) {
        REXLOG_WARN("ncaabb10: could not default {}: {}", d.name, set.error().GetDescription());
      }
    }
    // The profile's name on the game's menus and save screens; Options changes it. The patched
    // SDK has the setting (its default is "User"), the stock one always says "User".
    if (settings.Find("kernel.gamertag")) {
      (void)settings.SetText(rex::SettingLayer::kTitle, "kernel.gamertag", "Player");
    }
    // SDL knows most controllers itself. The community gamecontrollerdb.txt adds rarer ones. The
    // SDK looks in app.root (the exe folder, or the user config folder when that one is read only),
    // so a file next to the exe is used from there too; with neither, skip the load instead of
    // warning every start.
    std::error_code ec;
    const std::filesystem::path app_root = REX_PATHS().config_file.Get().parent_path();
    const std::filesystem::path beside_exe =
        rex::platform::GetExecutableFolder() / "gamecontrollerdb.txt";
    std::string mappings;  // Empty: skip the load.
    if (std::filesystem::exists(app_root / "gamecontrollerdb.txt", ec)) {
      mappings = "keep";
    } else if (std::filesystem::exists(beside_exe, ec)) {
      try {
        mappings = beside_exe.string();  // The native encoding the SDK parses paths from.
      } catch (const std::exception&) {
        // A folder name the narrow encoding cannot hold: skip the file rather than misread it.
      }
    }
    if (mappings != "keep") {
      if (auto set = settings.SetText(rex::SettingLayer::kTitle, "hid.sdl.mappings_file", mappings);
          !set) {
        REXLOG_WARN("ncaabb10: could not set hid.sdl.mappings_file: {}",
                    set.error().GetDescription());
      }
    }
    return rex::Ok();
  }

  // Sound is not required: when the audio system cannot start (the Windows Audio service off, no
  // sound server on Linux) the SDK ran no audio driver and refused to start ("no audio driver is
  // available"). Without one it keeps the game's audio clock itself and the game runs silent; the
  // front end says so. A missing output device was already fine: the SDK retries it.
  rex::AppSysRequirements GetRequirements() const override {
    rex::AppSysRequirements needs = TitleApp::GetRequirements();
    needs.audio = false;
    return needs;
  }

  // A startup failure in the player's words, in a box with the game's name: the SDK's box is
  // titled "ReXGlue" and says "modules failed: module 'rexgpu' failed to init, ...". Errors once
  // the app runs keep the SDK's handling (a message in the game window).
  void ReportError(std::string_view message) override {
    const rex::app::AppPhase phase = GetPhase();
    if (phase != rex::app::AppPhase::kStart && phase != rex::app::AppPhase::kProviders) {
      TitleApp::ReportError(message);
      return;
    }
    App::ReportError(message);  // The log.
    if (IsHeadlessRequested()) {
      return;
    }
    std::string text;
    if (message.find("graphics provider") != std::string_view::npos) {
#if defined(_WIN32)
      text = "No graphics card on this computer can run the game: it needs one that supports "
             "Direct3D 12 (feature level 11_0).";
#else
      text = "No graphics card on this computer can run the game: it needs one with a Vulkan "
             "driver.";
#endif
      text += "\n\nInstalling the latest driver from the card's maker (NVIDIA, AMD or Intel) "
              "usually fixes this.";
    } else {
      text = "The game could not start.";
    }
    std::error_code ec;
    const std::filesystem::path logs = std::filesystem::absolute(log_root.Get(), ec);
    text += std::format("\n\nDetails: {}\n\nThe log is in {}", message,
                        rex::string::PathToUTF8(logs.lexically_normal()));
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NCAA Basketball 10", text.c_str(), nullptr);
  }

  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime& title) override {
    if (auto mounted = MountUtilityDrive(title); !mounted) {
      // Not fatal: the game runs without the utility drive, only slower to load.
      REXLOG_ERROR("ncaabb10: utility drive not mounted: {}", mounted.error().GetDescription());
    }
    auto& bindings = GetInput().GetKeyBindings();
    for (const auto& error : bindings.TakeParseErrors()) {
      REXLOG_WARN("ncaabb10: key binding: {}", error);
    }
    return TitleApp::OnTitleLaunch(title);
  }
};

// Init, then the launcher until the player chooses Play, then the title.
int GameMain(Ncaabb10App& app, const rex::CLIArgList& args) {
  // Init also succeeds when a shutdown request (Ctrl+C, a signal) ended startup before the modules
  // loaded; then there is no input or UI to use, only Shutdown.
  if (app.Init(args) && !rex::app::IsShutdownRequested()) {
    // Which build wrote this log: a log someone sends is read against that commit.
    REXLOG_INFO("ncaabb10: PC port {}, commit {}, {} build of {}", NCAABB10_VERSION, NCAABB10_GIT,
                NCAABB10_CONFIG, __DATE__);
    std::jthread feed = StartInputFeed(app.GetInput());  // Test scripts drive the launcher too.
    ncaabb10::frontend::InstallUI(app);
    if (ncaabb10::frontend::RunLauncher(app) && app.LaunchTitle()) {
      app.Run();
    }
    ncaabb10::frontend::UninstallUI();
  }  // The input feed stops here, before Shutdown.
  return app.Shutdown();
}

REX_TITLE_APP_MAIN(Ncaabb10App, GameMain)
