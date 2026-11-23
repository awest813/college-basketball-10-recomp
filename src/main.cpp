// ncaabb10 - ReXGlue Recompiled Project

#include <SDL3/SDL.h>

#include <rex/app/title_app.h>
#include <rex/filesystem/file_system.h>
#include <rex/hid/input_system.h>
#include <rex/input_keys.h>
#include <rex/kernel/title_runtime.h>
#include <rex/logging/log_category.h>
#include <rex/path_settings.h>
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

// Development aid for automated testing. When NCAABB10_INPUT_FILE names a file, a thread follows it
// and feeds each new line to the input system as a key event, with input capture forced on, so a
// test script can drive the game without touching the desktop's real keyboard focus. Lines:
//   down <vk>   key down (Windows virtual key code, decimal)
//   up <vk>     key up
// With --hid.mnk_mode=true the keys act as pad buttons through the hid.keybind_* settings.
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
    };
    for (const Default& d : kDefaults) {
      if (auto set = settings.SetText(rex::SettingLayer::kTitle, d.name, d.value); !set) {
        REXLOG_WARN("ncaabb10: could not default {}: {}", d.name, set.error().GetDescription());
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
    StartInputFeed(GetInput());
    return TitleApp::OnTitleLaunch(title);
  }
};

// Init, then the launcher until the player chooses Play, then the title.
int GameMain(Ncaabb10App& app, const rex::CLIArgList& args) {
  if (app.Init(args)) {
    ncaabb10::frontend::InstallUI(app);
    if (ncaabb10::frontend::RunLauncher(app) && app.LaunchTitle()) {
      app.Run();
    }
  }
  return app.Shutdown();
}

REX_TITLE_APP_MAIN(Ncaabb10App, GameMain)
