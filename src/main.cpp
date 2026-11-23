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

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "generated/default/ncaabb10_init.h"
#include "frontend.h"

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
  for (bool down : {true, false}) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = window;
    event.key.scancode = scancode;
    event.key.key = SDL_GetKeyFromScancode(scancode, mod, false);
    event.key.mod = mod;
    event.key.down = down;
    SDL_PushEvent(&event);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
}

// A virtual controller for test scripts, attached on first use. SDL lists it like a real pad, so
// it goes through the SDK's SDL input driver to the title and through the front end's menus.
SDL_Joystick* TestPad() {
  static SDL_Joystick* pad = [] {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "Test controller";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick* opened = id ? SDL_OpenJoystick(id) : nullptr;
    REXLOG_INFO("ncaabb10: test controller attached: {}", opened ? "yes" : SDL_GetError());
    // Let SDL and the input driver list it before its first press.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return opened;
  }();
  return pad;
}

// Development aid for automated testing. When NCAABB10_INPUT_FILE names a file, a thread follows it
// and feeds each new line to the input system as a key event, with input capture forced on, so a
// test script can drive the game without touching the desktop's real keyboard focus. Lines:
//   down <vk>   key down (Windows virtual key code, decimal)
//   up <vk>     key up
//   ui <vk> [alt]   a key press through the window's event queue
//   pad <button> <0|1>   a test controller button (SDL_GamepadButton number) up or down
//   axis <axis> <value>  a test controller axis (SDL_GamepadAxis number), -32768 to 32767
// With hid.mnk_mode on (the default) the keys act as pad buttons through hid.keybind_*.
// SDL ignores controllers while the window is in the background, so a script that drives the test
// controller sets SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1.
void StartInputFeed(rex::hid::IInputSystem& input) {
  const char* file = std::getenv("NCAABB10_INPUT_FILE");
  if (!file || !*file) {
    return;
  }
  std::string path = file;
  REXLOG_INFO("ncaabb10: following input file {}", path);
  std::thread([&input, path] {
    // Open, read the complete new lines and close on every poll: an open ifstream would keep
    // the writer from appending on Windows.
    std::streamoff consumed = 0;
    std::vector<std::string> lines;
    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      lines.clear();
      {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
          continue;
        }
        in.seekg(consumed);
        std::string line;
        while (std::getline(in, line) && !in.eof()) {  // Skip a partly written last line.
          consumed = in.tellg();
          lines.push_back(line);
        }
      }
      for (const auto& line : lines) {
        std::istringstream words(line);
        std::string verb;
        if (line.starts_with("pad ") || line.starts_with("axis ")) {
          int index = 0, value = 0;
          if (!(words >> verb >> index >> value)) {
            continue;
          }
          if (SDL_Joystick* pad = TestPad()) {
            if (verb == "pad") {
              SDL_SetJoystickVirtualButton(pad, index, value != 0);
            } else {
              SDL_SetJoystickVirtualAxis(pad, index, static_cast<Sint16>(value));
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(60));
          auto state = input.GetState(0);
          REXLOG_INFO("ncaabb10: input feed {} {} {} -> user 0 buttons {:#06x} lx {} ly {} lt {}",
                      verb, index, value, state ? state->state.buttons : 0xFFFF,
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
  }).detach();
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
        // The keyboard is player 1's controller too, so the game is playable without a pad. A
        // connected pad still works: the keyboard merges into user 0 with the first pad.
        {"hid.mnk_mode", "true"},
    };
    for (const Default& d : kDefaults) {
      if (auto set = settings.SetText(rex::SettingLayer::kTitle, d.name, d.value); !set) {
        REXLOG_WARN("ncaabb10: could not default {}: {}", d.name, set.error().GetDescription());
      }
    }
    // SDL knows most controllers itself. The community gamecontrollerdb.txt adds rarer ones when
    // a player puts it next to the exe; without it, skip the load instead of warning every start.
    std::error_code ec;
    if (!std::filesystem::exists(rex::platform::GetExecutableFolder() / "gamecontrollerdb.txt", ec)) {
      if (auto set = settings.SetText(rex::SettingLayer::kTitle, "hid.sdl.mappings_file", ""); !set) {
        REXLOG_WARN("ncaabb10: could not clear hid.sdl.mappings_file: {}",
                    set.error().GetDescription());
      }
    }
    return rex::Ok();
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
    for (size_t i = 0; i < bindings.GetCount(); ++i) {
      for (const auto& source : bindings.GetSources(i)) {
        REXLOG_INFO("ncaabb10: binding {} source type {} code {:#x}", i,
                    static_cast<int>(source.type), static_cast<unsigned>(source.code));
      }
    }
    for (const char* name : {"keybind_start", "hid.keybind_start"}) {
      auto found = bindings.FindBySetting(name);
      REXLOG_INFO("ncaabb10: FindBySetting({}) = {}", name, found ? static_cast<int>(*found) : -1);
    }
    return TitleApp::OnTitleLaunch(title);
  }
};

// Init, then the launcher until the player chooses Play, then the title.
int GameMain(Ncaabb10App& app, const rex::CLIArgList& args) {
  if (app.Init(args)) {
    StartInputFeed(app.GetInput());  // Test scripts drive the launcher too.
    ncaabb10::frontend::InstallUI(app);
    if (ncaabb10::frontend::RunLauncher(app) && app.LaunchTitle()) {
      app.Run();
    }
    ncaabb10::frontend::UninstallUI();
  }
  return app.Shutdown();
}

REX_TITLE_APP_MAIN(Ncaabb10App, GameMain)
