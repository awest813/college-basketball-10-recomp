// ncaabb10 - player-facing front end: the launcher, the Options overlay (F1) and the fullscreen
// toggle (Alt+Enter or F11). Settings changed here apply at once and are saved to the config file.

#include "frontend.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>

#include <rex/app/title_app.h>
#include <rex/bit.h>
#include <rex/input_keys.h>
#include <rex/logging/log_category.h>
#include <rex/path_settings.h>
#include <rex/setting.h>
#include <rex/settings_registry.h>
#include <rex/ui/dialog.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/ui_system.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace ncaabb10::frontend {
namespace {

namespace fs = std::filesystem;

// The only executable the recompiled code matches: NCAA Basketball 10 (USA), Title ID 454108C3.
constexpr char kXexSha256[] = "3201c28392f73af9c969a740181996e954d3c894a7117eb81f8ff50362de79ef";

class FrontendSettings final : public rex::SettingOwner {
 public:
  FrontendSettings() : SettingOwner("ncaabb10") {}
  REX_SETTING(bool, show_launcher, true,
              "Show the launcher at startup; hold Shift while starting to show it anyway");
};

FrontendSettings& Settings() {
  static FrontendSettings settings;
  return settings;
}

rex::SettingsRegistry* g_registry = nullptr;

// ---------------------------------------------------------------------------------------------
// Settings helpers

std::string GetText(std::string_view name) {
  if (!g_registry) {
    return {};
  }
  auto found = g_registry->Find(name);
  if (!found) {
    return {};
  }
  std::string text = (*found)->GetText();
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    text = text.substr(1, text.size() - 2);
  }
  return text;
}

// The player's choices live in ncaabb10_options.toml next to the config file: only the settings
// changed through the launcher or Options, so project defaults keep applying to everything else
// (the SDK's Save writes every non-default value, command line ones included).
std::set<std::string> g_user_settings;

fs::path OptionsFile() {
  return REX_PATHS().config_file.Get().parent_path() / "ncaabb10_options.toml";
}

void Save() {
  if (!g_registry) {
    return;
  }
  std::map<std::string, std::vector<std::string>> sections;
  for (const std::string& name : g_user_settings) {
    auto found = g_registry->Find(name);
    if (!found) {
      continue;
    }
    const size_t dot = name.rfind('.');
    std::string text = (*found)->GetText();
    switch ((*found)->GetType()) {
      case rex::SettingType::kBool:
      case rex::SettingType::kI32:
      case rex::SettingType::kI64:
      case rex::SettingType::kU32:
      case rex::SettingType::kU64:
      case rex::SettingType::kF64:
        break;
      default:
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
          text = text.substr(1, text.size() - 2);
        }
        if (text.find('\'') != std::string::npos) {
          continue;  // A TOML literal string cannot hold a quote; skip rather than corrupt.
        }
        text = "'" + text + "'";
        break;
    }
    sections[name.substr(0, dot)].push_back(name.substr(dot + 1) + " = " + text);
  }
  const fs::path path = OptionsFile();
  const fs::path temp = path.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::trunc);
    out << "# NCAA Basketball 10 options, written by the launcher and the Options overlay (F1).\n";
    for (const auto& [section, lines] : sections) {
      out << "\n[" << section << "]\n";
      for (const std::string& line : lines) {
        out << line << "\n";
      }
    }
    if (!out) {
      REXLOG_WARN("ncaabb10: could not write {}", temp.string());
      return;
    }
  }
  std::error_code ec;
  fs::rename(temp, path, ec);
  if (ec) {
    REXLOG_WARN("ncaabb10: could not replace {}: {}", path.string(), ec.message());
  }
}

// Reads the names saved in the options file (the one format this file writes).
void LoadUserSettingNames() {
  std::ifstream in(OptionsFile());
  std::string line, section;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line[0] == '[') {
      section = line.substr(1, line.find(']') - 1);
      continue;
    }
    const size_t eq = line.find(" = ");
    if (eq != std::string::npos && !section.empty()) {
      g_user_settings.insert(section + "." + line.substr(0, eq));
    }
  }
}

void SetAndSave(std::string_view name, std::string_view text) {
  if (!g_registry) {
    return;
  }
  if (auto set = g_registry->SetText(name, text); !set) {
    REXLOG_WARN("ncaabb10: setting {}={} failed: {}", name, text, set.error().GetDescription());
    return;
  }
  REXLOG_INFO("ncaabb10: option {} = {}", name, text);
  g_user_settings.insert(std::string(name));
  Save();
}

bool GetBool(std::string_view name) { return GetText(name) == "true"; }

double GetDouble(std::string_view name, double fallback) {
  const std::string text = GetText(name);
  try {
    return text.empty() ? fallback : std::stod(text);
  } catch (...) {
    return fallback;
  }
}

// ---------------------------------------------------------------------------------------------
// SHA-256 (FIPS 180-4), portable so the launcher works the same on every platform.

class Sha256 {
 public:
  void Update(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      block_[block_size_++] = data[i];
      if (block_size_ == 64) {
        Transform();
        block_size_ = 0;
      }
    }
    length_bits_ += static_cast<uint64_t>(size) * 8;
  }

  std::string Final() {
    const uint64_t bits = length_bits_;
    const uint8_t pad = 0x80;
    Update(&pad, 1);
    const uint8_t zero = 0;
    while (block_size_ != 56) {
      Update(&zero, 1);
    }
    for (int i = 7; i >= 0; --i) {
      const uint8_t b = static_cast<uint8_t>(bits >> (i * 8));
      Update(&b, 1);
    }
    std::string hex;
    for (uint32_t v : state_) {
      char part[9];
      std::snprintf(part, sizeof(part), "%08x", v);
      hex += part;
    }
    return hex;
  }

 private:
  static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void Transform() {
    static constexpr uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = uint32_t(block_[i * 4]) << 24 | uint32_t(block_[i * 4 + 1]) << 16 |
             uint32_t(block_[i * 4 + 2]) << 8 | uint32_t(block_[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4],
             f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = h + s1 + ch + k[i] + w[i];
      const uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + maj;
      h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    state_[0] += a, state_[1] += b, state_[2] += c, state_[3] += d;
    state_[4] += e, state_[5] += f, state_[6] += g, state_[7] += h;
  }

  std::array<uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block_[64] = {};
  size_t block_size_ = 0;
  uint64_t length_bits_ = 0;
};

std::string FileSha256(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  Sha256 sha;
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    sha.Update(reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<size_t>(in.gcount()));
  }
  return sha.Final();
}

enum class GameCheck { kMissing, kWrongVersion, kOk };

GameCheck CheckGameFolder(const fs::path& folder) {
  std::error_code ec;
  const fs::path xex = folder / "default.xex";
  if (folder.empty() || !fs::is_regular_file(xex, ec)) {
    return GameCheck::kMissing;
  }
  return FileSha256(xex) == kXexSha256 ? GameCheck::kOk : GameCheck::kWrongVersion;
}

// ---------------------------------------------------------------------------------------------
// Xbox 360 disc image (XDVDFS) extraction.

class DiscExtractor {
 public:
  void Start(fs::path iso, fs::path destination) {
    done_ = false;
    failed_ = false;
    bytes_done_ = 0;
    bytes_total_ = 0;
    thread_ = std::thread([this, iso, destination] { Run(iso, destination); });
  }

  ~DiscExtractor() {
    cancel_ = true;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void Cancel() { cancel_ = true; }
  bool Running() const { return thread_.joinable() && !done_; }
  bool Done() const { return done_; }
  bool Failed() const { return failed_; }
  float Progress() const {
    const uint64_t total = bytes_total_;
    return total ? static_cast<float>(static_cast<double>(bytes_done_) / total) : 0.0f;
  }
  std::string Status() const {
    std::lock_guard lock(mutex_);
    return status_;
  }
  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  struct Entry {
    std::string path;
    uint32_t sector;
    uint32_t size;
  };

  static constexpr uint64_t kSector = 2048;

  void SetStatus(std::string text) {
    std::lock_guard lock(mutex_);
    status_ = std::move(text);
  }

  void Fail(std::string text) {
    SetStatus(std::move(text));
    failed_ = true;
    done_ = true;
  }

  // Walks one directory table, a binary tree of entries addressed in 4-byte units.
  bool Walk(std::ifstream& in, uint64_t base, uint32_t sector, uint32_t size, const std::string& prefix,
            std::vector<Entry>& out, int depth) {
    if (size == 0 || depth > 32) {
      return true;
    }
    std::vector<uint8_t> table(size);
    in.seekg(static_cast<std::streamoff>(base + sector * kSector));
    in.read(reinterpret_cast<char*>(table.data()), size);
    if (!in) {
      return false;
    }
    std::vector<uint32_t> stack = {0};
    std::vector<bool> seen(size / 4 + 1, false);
    while (!stack.empty()) {
      const uint32_t offset = stack.back();
      stack.pop_back();
      if (offset + 14 > size || seen[offset / 4]) {
        continue;
      }
      seen[offset / 4] = true;
      auto u16 = [&](uint32_t at) { return uint32_t(table[at]) | uint32_t(table[at + 1]) << 8; };
      auto u32 = [&](uint32_t at) { return u16(at) | u16(at + 2) << 16; };
      const uint32_t left = u16(offset), right = u16(offset + 2);
      if (left == 0xFFFF && right == 0xFFFF) {
        continue;
      }
      const uint32_t start = u32(offset + 4), length = u32(offset + 8);
      const uint8_t attributes = table[offset + 12], name_length = table[offset + 13];
      if (offset + 14 + name_length > size) {
        continue;
      }
      const std::string name(reinterpret_cast<const char*>(&table[offset + 14]), name_length);
      if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
          name.find('\\') != std::string::npos) {
        continue;  // Never write outside the destination.
      }
      if (attributes & 0x10) {
        if (!Walk(in, base, start, length, prefix + name + "/", out, depth + 1)) {
          return false;
        }
      } else {
        out.push_back({prefix + name, start, length});
      }
      if (left) {
        stack.push_back(left * 4);
      }
      if (right) {
        stack.push_back(right * 4);
      }
    }
    return true;
  }

  void Run(fs::path iso, fs::path destination) {
    std::ifstream in(iso, std::ios::binary);
    if (!in) {
      return Fail("Could not open the disc image.");
    }
    // Game partition offsets: plain XISO, XGD3, XGD1, XGD2.
    constexpr uint64_t kPartitions[] = {0, 0x2080000, 0xFD90000, 0x18300000};
    uint64_t base = UINT64_MAX;
    char magic[20];
    for (uint64_t candidate : kPartitions) {
      in.clear();
      in.seekg(static_cast<std::streamoff>(candidate + 32 * kSector));
      in.read(magic, sizeof(magic));
      if (in && std::memcmp(magic, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
        base = candidate;
        break;
      }
    }
    if (base == UINT64_MAX) {
      return Fail("This file is not an Xbox 360 disc image.");
    }
    uint8_t root[8];
    in.read(reinterpret_cast<char*>(root), 8);
    const uint32_t root_sector = root[0] | root[1] << 8 | root[2] << 16 | uint32_t(root[3]) << 24;
    const uint32_t root_size = root[4] | root[5] << 8 | root[6] << 16 | uint32_t(root[7]) << 24;
    std::vector<Entry> entries;
    SetStatus("Reading the disc's file list...");
    if (!Walk(in, base, root_sector, root_size, "", entries, 0)) {
      return Fail("The disc image's file list is damaged.");
    }
    uint64_t total = 0;
    bool has_xex = false;
    for (const Entry& e : entries) {
      total += e.size;
      has_xex |= e.path == "default.xex";
    }
    if (!has_xex) {
      return Fail("This disc image has no default.xex.");
    }
    bytes_total_ = total;
    std::vector<char> buffer(16 << 20);
    for (const Entry& e : entries) {
      if (cancel_) {
        return Fail("Canceled.");
      }
      SetStatus(e.path);
      const fs::path out_path = destination / fs::u8path(e.path);
      std::error_code ec;
      fs::create_directories(out_path.parent_path(), ec);
      std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
      if (!out) {
        return Fail("Could not write " + e.path + ". Is there enough free space?");
      }
      in.clear();
      in.seekg(static_cast<std::streamoff>(base + e.sector * kSector));
      uint64_t remaining = e.size;
      while (remaining) {
        if (cancel_) {
          return Fail("Canceled.");
        }
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        in.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!in) {
          return Fail("Read error in the disc image at " + e.path + ".");
        }
        out.write(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!out) {
          return Fail("Write error at " + e.path + ". Is there enough free space?");
        }
        remaining -= chunk;
        bytes_done_ += chunk;
      }
    }
    SetStatus("Checking default.xex...");
    switch (CheckGameFolder(destination)) {
      case GameCheck::kOk:
        SetStatus("Done.");
        done_ = true;
        return;
      case GameCheck::kWrongVersion:
        return Fail("This is a different version of the game than the one this port was made for "
                    "(NCAA Basketball 10, USA, Title ID 454108C3).");
      case GameCheck::kMissing:
        return Fail("default.xex is missing after extraction.");
    }
  }

  std::thread thread_;
  std::atomic<bool> cancel_{false}, done_{false}, failed_{false};
  std::atomic<uint64_t> bytes_done_{0}, bytes_total_{0};
  mutable std::mutex mutex_;
  std::string status_;
};

// ---------------------------------------------------------------------------------------------
// Options overlay

const char* const kAspectRatios[] = {"stretch", "16:9", "16:10", "21:9", "4:3"};
const char* const kAnisoLabels[] = {"Game default", "Off", "2x", "4x", "8x", "16x"};
const char* const kAnisoValues[] = {"-1", "0", "2", "3", "4", "5"};

void Checkbox(const char* label, const char* setting, const char* help = nullptr) {
  bool value = GetBool(setting);
  if (ImGui::Checkbox(label, &value)) {
    SetAndSave(setting, value ? "true" : "false");
  }
  if (help && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", help);
  }
}

template <size_t N>
void Combo(const char* label, const char* setting, const char* const (&labels)[N],
           const char* const (&values)[N]) {
  const std::string current = GetText(setting);
  int index = 0;
  for (size_t i = 0; i < N; ++i) {
    if (current == values[i]) {
      index = static_cast<int>(i);
    }
  }
  if (ImGui::Combo(label, &index, labels, static_cast<int>(N))) {
    SetAndSave(setting, values[index]);
  }
}

void Slider(const char* label, const char* setting, float min, float max, const char* format) {
  float value = static_cast<float>(GetDouble(setting, min));
  if (ImGui::SliderFloat(label, &value, min, max, format)) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", value);
    SetAndSave(setting, text);
  }
}

class OptionsDialog final : public rex::ui::DialogBase {
 public:
  explicit OptionsDialog(rex::ui::IUISystem& ui) : ui_(ui) {}

  bool Draw(ImGuiIO& io) override {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin("Options##ncaabb10", &open, ImGuiWindowFlags_NoCollapse)) {
      if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        Checkbox("Fullscreen (Alt+Enter, F11)", "ui.fullscreen");
        Checkbox("Exclusive fullscreen", "ui.fullscreen_exclusive",
                 "Changes the display mode instead of using a borderless window");
        static const char* const ratio_labels[] = {"Stretch", "16:9", "16:10", "21:9", "4:3"};
        Combo("Aspect ratio", "ui.aspect_ratio", ratio_labels, kAspectRatios);
        static const char* const scale_labels[] = {"1x (720p)", "2x (1440p)", "3x (2160p)"};
        static const char* const scale_values[] = {"1", "2", "3"};
        const std::string scale = GetText("gpu.resolution_scale_x");
        int scale_index = scale == "3" ? 2 : scale == "2" ? 1 : 0;
        if (ImGui::Combo("Render resolution *", &scale_index, scale_labels, 3)) {
          SetAndSave("gpu.resolution_scale_x", scale_values[scale_index]);
          SetAndSave("gpu.resolution_scale_y", scale_values[scale_index]);
        }
      }
      if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen)) {
        Combo("Texture filtering", "gpu.anisotropic_override", kAnisoLabels, kAnisoValues);
        Checkbox("Wait for display vsync", "gpu.vsync",
                 "Off is smoother: the game paces itself at 60 fps");
        static const char* const path_labels[] = {"Fast (host render targets)",
                                                  "Accurate (rasterizer ordered views)"};
        static const char* const path_values[] = {"rtv", "rov"};
        Combo("Render path *", "gpu.d3d12.render_target_path", path_labels, path_values);
      }
      if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        Slider("Volume", "apu.gain", 0.0f, 2.0f, "%.2f");
        Checkbox("Mute", "apu.mute");
        Checkbox("Mute when the window is in the background", "apu.mute_unfocused");
      }
      if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        Checkbox("Keyboard as controller", "hid.mnk_mode",
                 "Return Start, Space A, Backspace B, arrows D-pad, W/A/S/D left stick");
        Checkbox("Vibration", "hid.vibration");
        Slider("Left stick deadzone", "hid.left_stick_deadzone", 0.0f, 0.5f, "%.2f");
        Slider("Right stick deadzone", "hid.right_stick_deadzone", 0.0f, 0.5f, "%.2f");
      }
      if (ImGui::CollapsingHeader("Launcher")) {
        bool show = Settings().show_launcher.Get();
        if (ImGui::Checkbox("Show the launcher at startup", &show)) {
          SetAndSave("ncaabb10.show_launcher", show ? "true" : "false");
        }
      }
      ImGui::Separator();
      ImGui::TextDisabled("Saved to %s. * applies after a restart.",
                          REX_PATHS().config_file.Get().filename().string().c_str());
      if (ImGui::Button("All settings (F4)")) {
        ui_.ToggleOverlay("settings");
      }
      ImGui::SameLine();
      if (ImGui::Button("Close")) {
        open = false;
      }
    }
    ImGui::End();
    return open;
  }

  bool WantsExclusiveInput() const override { return true; }

 private:
  rex::ui::IUISystem& ui_;
};

// ---------------------------------------------------------------------------------------------
// Fullscreen hotkeys: overlays that never show. Their binds (ui.bind.fullscreen = F11,
// ui.bind.fullscreen_alt = Alt+Enter) go through the same bind table as F1 and F4, and OnToggle
// flips ui.fullscreen instead of showing a window.

class FullscreenToggle final : public rex::ui::DialogBase {
 public:
  bool Draw(ImGuiIO&) override { return false; }
  bool OnToggle() override {
    REXLOG_INFO("ncaabb10: fullscreen toggle (was {})", GetText("ui.fullscreen"));
    SetAndSave("ui.fullscreen", GetBool("ui.fullscreen") ? "false" : "true");
    return true;  // Handled; stay hidden.
  }
};

std::unique_ptr<rex::ui::IDialog> CreateFullscreenToggle(rex::ui::IUISystem&) {
  return std::make_unique<FullscreenToggle>();
}

// ---------------------------------------------------------------------------------------------
// Launcher

class LauncherDialog final : public rex::ui::DialogBase {
 public:
  enum class Result { kNone, kPlay, kQuit };

  LauncherDialog(rex::ui::IUISystem& ui, fs::path folder, fs::path default_folder)
      : ui_(ui), folder_(std::move(folder)), default_folder_(std::move(default_folder)) {
    Recheck();
  }

  Result result() const { return result_; }
  const fs::path& folder() const { return folder_; }

  bool Draw(ImGuiIO& io) override {
    if (extractor_ && extractor_->Done()) {
      extractor_->Join();
      if (extractor_->Failed()) {
        message_ = extractor_->Status();
      } else {
        folder_ = extract_to_;
        Recheck();
        message_ = "The game is set up.";
      }
      extractor_.reset();
    }

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Always);
    ImGui::Begin("NCAA Basketball 10##launcher", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("NCAA Basketball 10 - PC port (ReXGlue recompilation)");
    ImGui::Separator();

    if (extractor_) {
      ImGui::TextUnformatted("Copying the game from the disc image...");
      ImGui::ProgressBar(extractor_->Progress(), ImVec2(-1, 0));
      ImGui::TextDisabled("%s", extractor_->Status().c_str());
      if (ImGui::Button("Cancel")) {
        extractor_->Cancel();
      }
    } else {
      switch (check_) {
        case GameCheck::kOk:
          ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "Game files found and verified");
          break;
        case GameCheck::kWrongVersion:
          ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1),
                             "These game files are a different version (need the USA disc)");
          break;
        case GameCheck::kMissing:
          ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "The game is not set up yet");
          break;
      }
      ImGui::TextDisabled("%s", folder_.empty() ? "(no folder)" : folder_.string().c_str());
      if (!message_.empty()) {
        ImGui::TextWrapped("%s", message_.c_str());
      }
      ImGui::Spacing();

      ImGui::BeginDisabled(check_ != GameCheck::kOk);
      if (ImGui::Button("Play", ImVec2(160, 40))) {
        result_ = Result::kPlay;
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Options", ImVec2(110, 40))) {
        ui_.ToggleOverlay("options");
      }
      ImGui::SameLine();
      if (ImGui::Button("Quit", ImVec2(110, 40))) {
        result_ = Result::kQuit;
      }
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextUnformatted("Set up the game from your own disc:");
      if (ImGui::Button("Disc image (.iso)...")) {
        PickIso();
      }
      ImGui::SameLine();
      if (ImGui::Button("Extracted folder...")) {
        PickFolder();
      }
      bool show = Settings().show_launcher.Get();
      if (ImGui::Checkbox("Show this launcher at startup (hold Shift to show it anyway)", &show)) {
        SetAndSave("ncaabb10.show_launcher", show ? "true" : "false");
      }
    }
    ImGui::End();
    return result_ == Result::kNone;
  }

  bool WantsExclusiveInput() const override { return true; }

 private:
  void Recheck() { check_ = CheckGameFolder(folder_); }

  void PickIso() {
    rex::ui::FileDialogConfig config;
    config.mode = rex::ui::FileDialogConfig::Mode::kOpenFile;
    config.title = "Choose your NCAA Basketball 10 disc image";
    config.filters = {{"Xbox 360 disc image", "iso"}};
    auto chosen = ui_.ShowFileDialog(config);
    if (!chosen || chosen->empty()) {
      return;
    }
    std::error_code ec;
    const auto space = fs::space(default_folder_.parent_path(), ec);
    if (!ec && space.available < (uint64_t(7) << 30)) {
      message_ = "About 6 GB of free space is needed next to the game for the copied files.";
      return;
    }
    extract_to_ = default_folder_;
    message_.clear();
    extractor_ = std::make_unique<DiscExtractor>();
    extractor_->Start(chosen->front(), extract_to_);
  }

  void PickFolder() {
    rex::ui::FileDialogConfig config;
    config.mode = rex::ui::FileDialogConfig::Mode::kOpenFolder;
    config.title = "Choose the folder with default.xex";
    auto chosen = ui_.ShowFileDialog(config);
    if (!chosen || chosen->empty()) {
      return;
    }
    folder_ = chosen->front();
    Recheck();
    message_ = check_ == GameCheck::kOk ? "The game is set up." : "";
  }

  rex::ui::IUISystem& ui_;
  fs::path folder_;
  fs::path default_folder_;
  fs::path extract_to_;
  GameCheck check_ = GameCheck::kMissing;
  std::string message_;
  std::unique_ptr<DiscExtractor> extractor_;
  Result result_ = Result::kNone;
};

bool ShiftHeld() {
#if defined(_WIN32)
  return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#else
  return false;
#endif
}

}  // namespace

void Configure(rex::SettingsRegistry& settings) {
  g_registry = &settings;
  if (auto registered = settings.Register(Settings()); !registered) {
    REXLOG_WARN("ncaabb10: registering front end settings failed: {}",
                registered.error().GetDescription());
  }
  // The player's options, at the config file layer: the environment and command line still win.
  std::error_code ec;
  if (fs::exists(OptionsFile(), ec)) {
    LoadUserSettingNames();
    if (auto loaded = settings.Load(OptionsFile(), rex::SettingLayer::kFile); !loaded) {
      REXLOG_WARN("ncaabb10: loading {} failed: {}", OptionsFile().string(),
                  loaded.error().GetDescription());
    }
  }
}

void InstallUI(rex::app::TitleApp& app) {
  rex::ui::IUISystem& ui = app.GetUI();
  static constexpr rex::ui::OverlayConfig kOverlays[] = {
      {"options", {rex::VirtualKey::kF1, rex::KeyModifiers::kNone},
       [](rex::ui::IUISystem& ui) -> std::unique_ptr<rex::ui::IDialog> {
         return std::make_unique<OptionsDialog>(ui);
       }},
      {"fullscreen", {rex::VirtualKey::kF11, rex::KeyModifiers::kNone}, &CreateFullscreenToggle},
      {"fullscreen_alt", {rex::VirtualKey::kReturn, rex::KeyModifiers::kAlt}, &CreateFullscreenToggle},
  };
  if (auto added = ui.AddOverlays(kOverlays); !added) {
    REXLOG_WARN("ncaabb10: adding the Options overlay failed: {}", added.error().GetDescription());
  }
}

bool RunLauncher(rex::app::TitleApp& app) {
  const fs::path default_folder = REX_PATHS().user_root.Get().parent_path() / "game";
  fs::path folder = GetText("title.game_root");
  if (folder.empty() || CheckGameFolder(folder) == GameCheck::kMissing) {
    if (CheckGameFolder(default_folder) != GameCheck::kMissing) {
      folder = default_folder;
    }
  }
  const GameCheck check = CheckGameFolder(folder);
  if (check == GameCheck::kOk && !Settings().show_launcher.Get() && !ShiftHeld()) {
    // This session only: Save would also persist command line values such as a test's flags.
    if (GetText("title.game_root") != folder.string() && g_registry) {
      (void)g_registry->SetText("title.game_root", folder.string());
    }
    return true;
  }
  if (app.GetUI().IsHeadless()) {
    if (check == GameCheck::kOk) {
      return true;
    }
    REXLOG_ERROR("ncaabb10: no verified game files; set title.game_root");
    return false;
  }

  auto launcher = std::make_unique<LauncherDialog>(app.GetUI(), folder, default_folder);
  LauncherDialog* view = launcher.get();
  rex::ScopeGuard shown = app.GetUI().AddDialog(*view);
  while (view->result() == LauncherDialog::Result::kNone && app.RunFrame()) {
  }
  if (view->result() != LauncherDialog::Result::kPlay) {
    return false;
  }
  if (GetText("title.game_root") != view->folder().string()) {
    SetAndSave("title.game_root", view->folder().string());
  }
  return true;
}

}  // namespace ncaabb10::frontend
