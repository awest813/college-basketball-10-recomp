// ncaabb10 - player-facing front end: the launcher, the Options overlay (F1) and the fullscreen
// toggle (Alt+Enter or F11). Settings changed here apply at once and are saved to the config file.

#include "frontend.h"
#include "ncaabb10_git.h"  // NCAABB10_GIT, written by the build (cmake/git_rev.cmake)

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <imgui.h>

#include <rex/app/title_app.h>
#include <rex/bit.h>
#include <rex/hid/binding.h>
#include <rex/hid/input_system.h>
#include <rex/input_keys.h>
#include <rex/logging/log_category.h>
#include <rex/path_settings.h>
#include <rex/setting.h>
#include <rex/settings_registry.h>
#include <rex/string/text.h>
#include <rex/ui/dialog.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/ui_system.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_stdinc.h>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
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
rex::hid::IInputSystem* g_input = nullptr;  // Set by InstallUI; the key binding editor uses it.

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
// changed through the launcher or Options, so project defaults keep applying to everything else.
// Each is kept as the player set it (its TOML value text), never as the effective value, so command
// line and environment overrides are not saved; entries this build does not know (another GPU
// backend's settings) are carried through unchanged.
std::map<std::string, std::string> g_user_values;
// Values for settings that can no longer change in this run (kUntilLaunch ones once the title
// runs): saved to the file now, applied on the next start.
std::map<std::string, std::string> g_next_start;
// Each setting's text right after the options file or Options set it. A runtime-layer value that
// differs later was set elsewhere (F4's All settings); Save adopts it, so the last choice wins.
std::map<std::string, std::string> g_applied;

void Remember(const rex::SettingsRegistry& settings, const std::string& name) {
  if (auto found = settings.Find(name); found) {
    g_applied[name] = (*found)->GetText();
  }
}

// Fixed when the options load, so a --config_file given on the command line (parsed after that)
// does not make Save write somewhere other than the file that was read.
fs::path g_options_file;

fs::path OptionsFile() {
  if (g_options_file.empty()) {
    g_options_file = REX_PATHS().config_file.Get().parent_path() / "ncaabb10_options.toml";
  }
  return g_options_file;
}

// A path as UTF-8 text, for display and logs. fs::path::string() converts to the ANSI code page on
// Windows and throws for characters outside it.
std::string Utf8(const fs::path& path) { return rex::string::PathToUTF8(path); }

// A path as the text the SDK's path settings parse (the native narrow encoding), or nothing when
// the path has characters that encoding cannot hold.
std::optional<std::string> NativeText(const fs::path& path) {
  try {
    return path.string();
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string Trim(std::string_view text) {
  const size_t first = text.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) {
    return {};
  }
  const size_t last = text.find_last_not_of(" \t\r");
  return std::string(text.substr(first, last - first + 1));
}

// A TOML basic string.
std::string TomlString(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    if (static_cast<unsigned char>(c) < 0x20) {
      continue;  // No setting holds control characters.
    }
    if (c == '\\' || c == '"') {
      out += '\\';
    }
    out += c;
  }
  return out + "\"";
}

// The setting text a TOML value in the options file stands for: bare, 'literal' or "basic" (with
// the \\ and \" escapes TomlString writes).
std::string TomlValueText(std::string_view value) {
  if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
    return std::string(value.substr(1, value.size() - 2));
  }
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    std::string out;
    for (size_t i = 1; i + 1 < value.size(); ++i) {
      if (value[i] == '\\' && i + 2 < value.size()) {
        ++i;
      }
      out += value[i];
    }
    return out;
  }
  return std::string(value);
}

// The TOML value for a setting's text: bare for numbers and booleans, a string otherwise.
std::string TomlValue(std::string_view name, std::string_view text) {
  if (!g_registry) {
    return TomlString(text);
  }
  if (auto found = g_registry->Find(name); found) {
    switch ((*found)->GetType()) {
      case rex::SettingType::kBool:
      case rex::SettingType::kI32:
      case rex::SettingType::kI64:
      case rex::SettingType::kU32:
      case rex::SettingType::kU64:
      case rex::SettingType::kF64:
        return std::string(text);
      default:
        break;
    }
  }
  return TomlString(text);
}

// Takes in what F4 changed since Options or the file last set a setting Options manages; without
// this, writing the options file again (or the next start) would undo that change. True if any.
bool AdoptOtherChanges() {
  bool any = false;
  if (!g_registry) {
    return false;
  }
  for (auto& [name, value] : g_user_values) {
    auto found = g_registry->Find(name);
    auto applied = g_applied.find(name);
    if (!found || !*found || applied == g_applied.end() || (*found)->IsFrozen() ||
        (*found)->GetLayer() != rex::SettingLayer::kRuntime) {
      continue;
    }
    const std::string text = (*found)->GetText();
    if (text != applied->second) {
      REXLOG_INFO("ncaabb10: option {} = {} (changed in All settings)", name, text);
      value = TomlValue(name, text);
      applied->second = text;
      any = true;
    }
  }
  return any;
}

void Save() {
  AdoptOtherChanges();
  std::map<std::string, std::vector<std::string>> sections;
  for (const auto& [name, value] : g_user_values) {
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos) {
      continue;
    }
    sections[name.substr(0, dot)].push_back(name.substr(dot + 1) + " = " + value);
  }
  const fs::path path = OptionsFile();
  fs::path temp = path;
  temp += ".tmp";
  std::error_code ec;
  // app.root is the user config folder when the exe folder is read only; it may not exist yet.
  fs::create_directories(path.parent_path(), ec);
  {
    std::ofstream out(temp, std::ios::trunc);
    out << "# NCAA Basketball 10 options, written by the launcher and the Options overlay (F1).\n";
    for (const auto& [section, lines] : sections) {
      out << "\n[" << section << "]\n";
      for (const std::string& line : lines) {
        out << line << "\n";
      }
    }
    // The file is small enough to sit in the stream's buffer, so errors show up only on close.
    out.close();
    if (!out) {
      REXLOG_WARN("ncaabb10: could not write {}; the options stay as they were", Utf8(temp));
      fs::remove(temp, ec);
      return;
    }
  }
  fs::rename(temp, path, ec);
  if (ec) {
    REXLOG_WARN("ncaabb10: could not replace {}: {}", Utf8(path), ec.message());
  }
}

// Applies the options file (the format Save writes) entry by entry. It goes in at the environment
// layer: the SDK loads the config file (ncaabb10.toml, which F4's "Save to config" writes) after
// this, at the file layer, and a lower layer never replaces a higher one, so a value saved there
// once cannot undo a later choice in Options. Environment variables apply after this at the same
// layer, and the command line above it, so both still win. A value this build refuses is reported
// and dropped; the entries after it still apply. The other way round, a later change in F4 is
// taken into this file (AdoptOtherChanges), so neither undoes the other.
// Calls entry(name, value) for each setting in an options file, value as written (TOML text).
template <typename Fn>
void ReadOptions(const fs::path& file, Fn&& entry) {
  std::ifstream in(file);
  std::string line, section;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line[0] == '[') {
      section = Trim(line.substr(1, line.find(']') - 1));
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos || section.empty()) {
      continue;
    }
    entry(section + "." + Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)));
  }
}

void LoadUserSettings(rex::SettingsRegistry& settings) {
  ReadOptions(OptionsFile(), [&](const std::string& name, const std::string& value) {
    if (auto set = settings.SetText(rex::SettingLayer::kEnvironment, name, TomlValueText(value));
        !set) {
      REXLOG_WARN("ncaabb10: {} refused {} = {} ({}); dropping it", Utf8(OptionsFile()), name,
                  value, set.error().GetDescription());
      return;
    }
    g_user_values[name] = value;
    Remember(settings, name);
  });
}

void SetAndSave(std::string_view name, std::string_view text) {
  if (!g_registry) {
    return;
  }
  auto found = g_registry->Find(name);
  if (found && (*found)->IsFrozen() && (*found)->Check(text)) {
    REXLOG_INFO("ncaabb10: option {} = {} (on the next start)", name, text);
    g_next_start[std::string(name)] = std::string(text);
    g_user_values[std::string(name)] = TomlValue(name, text);
    Save();
    return;
  }
  if (auto set = g_registry->SetText(name, text); !set) {
    REXLOG_WARN("ncaabb10: setting {}={} failed: {}", name, text, set.error().GetDescription());
    return;
  }
  REXLOG_INFO("ncaabb10: option {} = {}", name, text);
  g_user_values[std::string(name)] = TomlValue(name, text);
  Remember(*g_registry, std::string(name));
  Save();
}

bool GetBool(std::string_view name) { return GetText(name) == "true"; }

// The value Options shows: a change waiting for the next start, else the current value. A setting
// that is fixed for this run keeps its old value until then, and showing that would look as if the
// choice had been refused.
std::string GetShown(std::string_view name) {
  const auto next = g_next_start.find(std::string(name));
  return next != g_next_start.end() ? next->second : GetText(name);
}

// The player name, including a change waiting for the next start.
std::string GetGamertag() { return GetShown("kernel.gamertag"); }

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

// Hashes default.xex once per file version: the launcher checks the same folder several times.
GameCheck CheckGameFolder(const fs::path& folder) {
  std::error_code ec;
  const fs::path xex = folder / "default.xex";
  if (folder.empty() || !fs::is_regular_file(xex, ec)) {
    return GameCheck::kMissing;
  }
  struct Seen {
    uintmax_t size;
    fs::file_time_type time;
    GameCheck check;
  };
  static std::mutex mutex;
  static std::map<fs::path, Seen> seen;
  std::error_code size_ec, time_ec, path_ec;
  const uintmax_t size = fs::file_size(xex, size_ec);
  const fs::file_time_type time = fs::last_write_time(xex, time_ec);
  const fs::path key = fs::absolute(xex, path_ec).lexically_normal();
  const bool cacheable = !size_ec && !time_ec && !path_ec;
  if (cacheable) {
    std::lock_guard lock(mutex);
    if (auto it = seen.find(key); it != seen.end() && it->second.size == size &&
                                  it->second.time == time) {
      return it->second.check;
    }
  }
  const GameCheck check = FileSha256(xex) == kXexSha256 ? GameCheck::kOk : GameCheck::kWrongVersion;
  if (cacheable) {
    std::lock_guard lock(mutex);
    seen[key] = {size, time, check};
  }
  return check;
}

// ---------------------------------------------------------------------------------------------
// Xbox 360 disc image (XDVDFS) extraction.

class DiscExtractor {
 public:
  void Start(fs::path iso, fs::path destination) {
    done_ = false;
    failed_ = false;
    canceled_ = false;
    install_moved_ = false;
    left_old_ = false;
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
  bool Canceling() const { return cancel_ && !done_; }
  // Stopped by Cancel, not by an error that came up while the cancel was on its way.
  bool Canceled() const { return canceled_; }
  // The previous install could not be put back and is still in <destination>.old (the status
  // says so): the game folder no longer holds a game.
  bool InstallMoved() const { return install_moved_; }
  // Set up, but some of the player's files could not be moved out of <destination>.old.
  bool LeftOld() const { return left_old_; }
  uint64_t BytesDone() const { return bytes_done_; }
  uint64_t BytesTotal() const { return bytes_total_; }
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
  std::set<uint32_t> walked_;  // Directory sectors read, so a looping image cannot recurse forever.
  std::set<std::string> names_;  // Every path listed, lowercase.
  uint64_t table_bytes_ = 0;     // Directory table bytes read.

  void SetStatus(std::string text) {
    std::lock_guard lock(mutex_);
    status_ = std::move(text);
  }

  void Fail(std::string text) {
    SetStatus(std::move(text));
    failed_ = true;
  }

  void FailCanceled() {
    canceled_ = true;
    Fail("Canceled.");
  }

  // Files in the old game folder that the new install does not have, such as the disc image
  // itself when the player kept it there, move into the new folder instead of being deleted with
  // the old install. Only the top level: below it, every folder is the game's own. False when one
  // could not be moved (or the folder not read): then the old folder must stay.
  static bool KeepOtherFiles(const fs::path& old, const fs::path& destination) {
    std::error_code ec;
    fs::directory_iterator it(old, ec);
    bool all = !ec;
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::error_code item_ec;
      const fs::path target = destination / it->path().filename();
      if (!fs::exists(target, item_ec) && !item_ec) {
        fs::rename(it->path(), target, item_ec);
      }
      if (item_ec) {
        REXLOG_WARN("ncaabb10: could not keep {}: {}", Utf8(it->path()), item_ec.message());
        all = false;
      }
    }
    return all && !ec;
  }

  // Extracts into <destination>.partial and swaps it in only once default.xex verifies, so a
  // canceled or failed copy never leaves a half-written game folder behind.
  // A rename can fail for a moment while an antivirus scanner still holds a freshly written file.
  static void RenameRetrying(const fs::path& from, const fs::path& to, std::error_code& ec) {
    for (int attempt = 0; attempt < 20; ++attempt) {
      fs::rename(from, to, ec);
      if (!ec) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
  }

  void Run(fs::path iso, fs::path destination) {
    fs::path work = destination;
    work += ".partial";
    fs::path old = destination;
    old += ".old";
    std::error_code ec;
    try {
      // A previous setup that could not put the old install back left it in .old: restore it
      // before anything below deletes .old. An empty destination counts as missing. If it cannot
      // be restored, stop: .old is then the only copy of that install.
      if (fs::exists(old, ec) && (!fs::exists(destination, ec) || fs::is_empty(destination, ec))) {
        fs::remove(destination, ec);
        RenameRetrying(old, destination, ec);
        if (ec) {
          install_moved_ = true;
          Fail("The previous game files are in " + Utf8(old) + " and could not be moved back to " +
               Utf8(destination) + " (" + ec.message() + "). Move them back, then try again.");
          done_ = true;
          return;
        }
      } else if (fs::exists(old, ec)) {
        // Left by an earlier setup that could not move some of the player's files out of it.
        install_moved_ = true;  // Not "nothing was changed": the player has to look at it.
        Fail(Utf8(old) + " is left from an earlier setup and may hold files of yours. Move out "
             "what you want to keep, delete that folder, then try again.");
        done_ = true;
        return;
      }
      fs::remove_all(work, ec);
      Extract(iso, work);
      if (!failed_) {
        fs::remove_all(old, ec);
        const bool had_install = fs::exists(destination, ec);
        ec.clear();
        if (had_install) {
          RenameRetrying(destination, old, ec);
        }
        if (ec) {
          Fail("Could not replace the game in " + Utf8(destination) + ": " + ec.message());
        } else {
          RenameRetrying(work, destination, ec);
          if (ec) {
            Fail("Could not move the copied game into " + Utf8(destination) + ": " + ec.message());
            if (had_install) {
              std::error_code restore_ec;
              RenameRetrying(old, destination, restore_ec);  // Put the old install back.
              if (restore_ec) {
                install_moved_ = true;
                Fail("Could not move the copied game into " + Utf8(destination) + ": " +
                     ec.message() + ". The previous game files are in " + Utf8(old) + ".");
              }
            }
          } else {
            if (had_install && !KeepOtherFiles(old, destination)) {
              left_old_ = true;  // The new game is in place; the rest stays for the player.
            } else {
              fs::remove_all(old, ec);
            }
          }
        }
      }
    } catch (const std::exception& e) {
      Fail(std::string("Disc setup stopped: ") + e.what());
    }
    if (failed_) {
      fs::remove_all(work, ec);
    }
    done_ = true;
  }

  // The disc's file names are printable ASCII that every host file system takes. Anything else
  // (a separator or a drive colon that would write outside the destination, "..", bytes that are
  // not text, a Windows device name such as NUL or COM1, a trailing dot or space that Windows
  // drops) means a damaged or altered image; skipping the entry would install a broken game.
  static bool PlainName(std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') {
      return false;
    }
    for (const char c : name) {
      if (c < 0x20 || c > 0x7E || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) {
        return false;
      }
    }
    // Device names are reserved with any extension ("nul.txt" too) and in any case.
    const std::string stem = Lower(name.substr(0, name.find('.')));
    static constexpr std::string_view kDevices[] = {"con", "prn", "aux", "nul", "conin$", "conout$"};
    for (const std::string_view device : kDevices) {
      if (stem == device) {
        return false;
      }
    }
    if (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
        stem[3] >= '0' && stem[3] <= '9') {
      return false;
    }
    return true;
  }

  static std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    return out;
  }

  // Walks one directory table, a binary tree of entries addressed in 4-byte units.
  bool Walk(std::ifstream& in, uint64_t base, uint32_t sector, uint32_t size, const std::string& prefix,
            std::vector<Entry>& out, int depth) {
    if (size == 0) {
      return true;
    }
    // The real disc nests a few folders deep; a damaged image could chain thousands of tables,
    // each up to 16 MB, so the scan stops for Cancel too.
    if (depth > 32 || cancel_) {
      return false;
    }
    // A real directory table is a few sectors; a damaged one must not loop or exhaust memory.
    // Nor, across all its tables, list more than a disc could hold.
    table_bytes_ += size;
    if (size > (16u << 20) || table_bytes_ > (64u << 20) || !walked_.insert(sector).second) {
      return false;
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
      // Two names that differ only in case would be one file on Windows: the second overwrites.
      if (!PlainName(name) || !names_.insert(Lower(prefix + name)).second ||
          names_.size() > 1000000) {
        return false;
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

  void Extract(const fs::path& iso, const fs::path& destination) {
    walked_.clear();
    names_.clear();
    table_bytes_ = 0;
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
      return cancel_ ? FailCanceled() : Fail("The disc image's file list is damaged.");
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
    // The copy needs its full size free: an existing install stays until the copy verifies.
    fs::path volume = destination.parent_path();
    std::error_code space_ec;
    while (!volume.empty() && !fs::exists(volume, space_ec) && volume.has_relative_path()) {
      volume = volume.parent_path();
    }
    const auto space = fs::space(volume, space_ec);
    const uint64_t needed = total + (uint64_t(64) << 20);
    if (!space_ec && space.available < needed) {
      char text[384];
      std::snprintf(text, sizeof(text), "Not enough free space in %s: the game needs %.1f GB, "
                    "%.1f GB is free.", Utf8(volume).c_str(), needed / 1e9, space.available / 1e9);
      return Fail(text);
    }
    bytes_total_ = total;
    std::vector<char> buffer(16 << 20);
    for (const Entry& e : entries) {
      if (cancel_) {
        return FailCanceled();
      }
      SetStatus(e.path);
      const fs::path out_path = destination / fs::path(std::u8string(e.path.begin(), e.path.end()));
      std::error_code ec;
      fs::create_directories(out_path.parent_path(), ec);
      errno = 0;
      std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
      if (!out) {
        const int error = errno;
        return Fail("Could not create " + e.path +
                    (error ? ": " + std::generic_category().message(error) + "." : "."));
      }
      in.clear();
      in.seekg(static_cast<std::streamoff>(base + e.sector * kSector));
      uint64_t remaining = e.size;
      while (remaining) {
        if (cancel_) {
          return FailCanceled();
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
      // The last part of each file is still in the stream's buffer: a write error there shows
      // up only on close, and would otherwise install a cut-short file.
      out.close();
      if (!out) {
        return Fail("Write error at " + e.path + ". Is there enough free space?");
      }
    }
    SetStatus("Checking default.xex...");
    switch (CheckGameFolder(destination)) {
      case GameCheck::kOk:
        SetStatus("Done.");
        return;
      case GameCheck::kWrongVersion:
        return Fail("This is a different version of the game than the one this port was made for "
                    "(NCAA Basketball 10, USA, Title ID 454108C3).");
      case GameCheck::kMissing:
        return Fail("default.xex is missing after extraction.");
    }
  }

  std::thread thread_;
  std::atomic<bool> cancel_{false}, done_{false}, failed_{false}, canceled_{false};
  std::atomic<bool> install_moved_{false};
  std::atomic<bool> left_old_{false};
  std::atomic<uint64_t> bytes_done_{0}, bytes_total_{0};
  mutable std::mutex mutex_;
  std::string status_;
};

// ---------------------------------------------------------------------------------------------
// Controllers
//
// The SDK feeds ImGui the keyboard and mouse only. The launcher and Options read the pads
// themselves, through SDL (the SDK's input driver keeps them open), so a controller can move
// between controls (D-pad or left stick), choose (A) and go back (B).

// The SDK's driver opens a pad only once the title reads it, so before Play the menus open the
// pads themselves. SDL counts opens: the driver's later open shares the same pad. UI thread only.
std::map<SDL_JoystickID, SDL_Gamepad*>& OwnedGamepads() {
  static std::map<SDL_JoystickID, SDL_Gamepad*> owned;
  return owned;
}

void OpenGamepads() {
  auto& owned = OwnedGamepads();
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  std::set<SDL_JoystickID> present;
  for (int i = 0; ids && i < count; ++i) {
    present.insert(ids[i]);
    if (!owned.contains(ids[i])) {
      if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[i])) {
        const char* name = SDL_GetGamepadName(pad);
        REXLOG_INFO("ncaabb10: controller connected: {}", name ? name : "?");
        owned[ids[i]] = pad;
      }
    }
  }
  SDL_free(ids);
  for (auto it = owned.begin(); it != owned.end();) {
    if (!present.contains(it->first)) {
      REXLOG_INFO("ncaabb10: controller disconnected");
      SDL_CloseGamepad(it->second);
      it = owned.erase(it);
    } else {
      ++it;
    }
  }
}

// Calls fn for each open pad. Any thread: it only reads pads someone already opened.
template <typename Fn>
void ForEachGamepad(Fn&& fn) {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  for (int i = 0; ids && i < count; ++i) {
    if (SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i])) {
      fn(pad);
    }
  }
  SDL_free(ids);
}

// "Xbox Series X Controller, PS5 Controller", or empty with no controller.
std::string GamepadNames() {
  std::string names;
  ForEachGamepad([&](SDL_Gamepad* pad) {
    const char* name = SDL_GetGamepadName(pad);
    names += (names.empty() ? "" : ", ") + std::string(name && *name ? name : "Controller");
  });
  return names;
}

// Once per ImGui frame, from the Draw of whichever front end window is showing.
void FeedGamepads(ImGuiIO& io) {
  static int fed_frame = -1;
  if (ImGui::GetFrameCount() == fed_frame) {
    return;
  }
  fed_frame = ImGui::GetFrameCount();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  OpenGamepads();

  // X and Y stay out: ImGui would take Y as "edit as text", which presses a focused button,
  // and X as its window menu. The front end has no text fields to edit.
  struct ButtonMap {
    SDL_GamepadButton sdl;
    ImGuiKey key;
  };
  static constexpr ButtonMap kButtons[] = {
      {SDL_GAMEPAD_BUTTON_START, ImGuiKey_GamepadStart},
      {SDL_GAMEPAD_BUTTON_BACK, ImGuiKey_GamepadBack},
      {SDL_GAMEPAD_BUTTON_SOUTH, ImGuiKey_GamepadFaceDown},
      {SDL_GAMEPAD_BUTTON_EAST, ImGuiKey_GamepadFaceRight},
      {SDL_GAMEPAD_BUTTON_DPAD_UP, ImGuiKey_GamepadDpadUp},
      {SDL_GAMEPAD_BUTTON_DPAD_DOWN, ImGuiKey_GamepadDpadDown},
      {SDL_GAMEPAD_BUTTON_DPAD_LEFT, ImGuiKey_GamepadDpadLeft},
      {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, ImGuiKey_GamepadDpadRight},
      {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, ImGuiKey_GamepadL1},
      {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, ImGuiKey_GamepadR1},
      {SDL_GAMEPAD_BUTTON_LEFT_STICK, ImGuiKey_GamepadL3},
      {SDL_GAMEPAD_BUTTON_RIGHT_STICK, ImGuiKey_GamepadR3},
  };
  struct AxisMap {
    SDL_GamepadAxis sdl;
    ImGuiKey key;
    float from, to;  // The raw range that maps to 0..1, starting past the deadzone.
  };
  constexpr float kStickDeadzone = 8000, kTriggerDeadzone = 3000;
  static constexpr AxisMap kAxes[] = {
      {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, ImGuiKey_GamepadL2, kTriggerDeadzone, 32767},
      {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, ImGuiKey_GamepadR2, kTriggerDeadzone, 32767},
      {SDL_GAMEPAD_AXIS_LEFTX, ImGuiKey_GamepadLStickLeft, -kStickDeadzone, -32768},
      {SDL_GAMEPAD_AXIS_LEFTX, ImGuiKey_GamepadLStickRight, kStickDeadzone, 32767},
      {SDL_GAMEPAD_AXIS_LEFTY, ImGuiKey_GamepadLStickUp, -kStickDeadzone, -32768},
      {SDL_GAMEPAD_AXIS_LEFTY, ImGuiKey_GamepadLStickDown, kStickDeadzone, 32767},
      {SDL_GAMEPAD_AXIS_RIGHTX, ImGuiKey_GamepadRStickLeft, -kStickDeadzone, -32768},
      {SDL_GAMEPAD_AXIS_RIGHTX, ImGuiKey_GamepadRStickRight, kStickDeadzone, 32767},
      {SDL_GAMEPAD_AXIS_RIGHTY, ImGuiKey_GamepadRStickUp, -kStickDeadzone, -32768},
      {SDL_GAMEPAD_AXIS_RIGHTY, ImGuiKey_GamepadRStickDown, kStickDeadzone, 32767},
  };
  bool down[std::size(kButtons)] = {};
  float amount[std::size(kAxes)] = {};
  bool any = false;
  ForEachGamepad([&](SDL_Gamepad* pad) {  // Every pad drives the menus.
    any = true;
    for (size_t i = 0; i < std::size(kButtons); ++i) {
      down[i] = down[i] || SDL_GetGamepadButton(pad, kButtons[i].sdl);
    }
    for (size_t i = 0; i < std::size(kAxes); ++i) {
      const float raw = SDL_GetGamepadAxis(pad, kAxes[i].sdl);
      const float v = (raw - kAxes[i].from) / (kAxes[i].to - kAxes[i].from);
      amount[i] = std::max(amount[i], std::min(v, 1.0f));
    }
  });
  if (any) {
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  } else {
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  }
  for (size_t i = 0; i < std::size(kButtons); ++i) {
    io.AddKeyEvent(kButtons[i].key, down[i]);
  }
  for (size_t i = 0; i < std::size(kAxes); ++i) {
    io.AddKeyAnalogEvent(kAxes[i].key, amount[i] > 0.1f, amount[i]);
  }
}

// Guide (the Xbox or PS button), or Back held while pressing Start, opens and closes Options
// from the controller, in the launcher and in game. A thread watches for it: no window is drawn
// in game for a Draw to check from.
class PadShortcuts {
 public:
  void Start(rex::ui::IUISystem& ui) {
    if (thread_.joinable() || ui.IsHeadless()) {
      return;
    }
    stop_ = false;
    thread_ = std::thread([this, &ui] {
      bool was_down = false;
      while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        bool down = false;
        ForEachGamepad([&](SDL_Gamepad* pad) {
          down = down || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_GUIDE) ||
                 (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK) &&
                  SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START));
        });
        if (down && !was_down && !stop_) {
          REXLOG_INFO("ncaabb10: controller Options shortcut");
          ui.PostTask([&ui] { ui.ToggleOverlay("options"); });
        }
        was_down = down;
      }
    });
  }

  void Stop() {
    stop_ = true;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  ~PadShortcuts() { Stop(); }

 private:
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

PadShortcuts g_pad_shortcuts;

// ---------------------------------------------------------------------------------------------
// Size
//
// The front end is laid out for a 720p window with the SDK's 10 px pixel font. On a large display
// (fullscreen at 1440p or 4K) it scales up in whole steps, which keep the pixel font crisp, so it
// is not a small box in the middle of a TV. Call at the top of each Draw; returns the scale for
// the explicit sizes the Draw passes to ImGui.
float ApplyUiScale(const ImGuiIO& io) {
  static float applied = 0.0f;
  static ImGuiStyle base;
  const float fit = std::min(io.DisplaySize.y / 600.0f, io.DisplaySize.x / 1000.0f);
  const float scale = std::clamp(std::floor(fit), 1.0f, 4.0f);
  if (scale != applied) {
    ImGuiStyle& style = ImGui::GetStyle();
    if (applied == 0.0f) {
      base = style;  // The SDK's own style, before any scaling.
    }
    style = base;
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;
    applied = scale;
  }
  return scale;
}

// Wrapped, dimmed text: paths are long and must not run past the window's edge.
void TextDimWrapped(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------------------------
// Options overlay

const char* const kAspectRatios[] = {"stretch", "16:9", "16:10", "21:9", "4:3"};
const char* const kAnisoLabels[] = {"Game default", "Off", "2x", "4x", "8x", "16x"};
const char* const kAnisoValues[] = {"-1", "0", "2", "3", "4", "5"};

void Checkbox(const char* label, const char* setting, const char* help = nullptr) {
  bool value = GetShown(setting) == "true";
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
  const std::string current = GetShown(setting);
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
  const bool changed = ImGui::SliderFloat(label, &value, min, max, format);
  char text[32];
  std::snprintf(text, sizeof(text), "%.3f", value);
  // Heard and felt while dragging; written to the options file once, on release.
  if (changed && ImGui::IsItemActive()) {
    if (g_registry) {
      (void)g_registry->SetText(setting, text);
    }
  } else if (changed || ImGui::IsItemDeactivatedAfterEdit()) {
    SetAndSave(setting, text);
  }
}

// The keyboard-as-controller bindings Options edits, in the order a pad's buttons are usually
// listed. Each is an SDK setting (hid.keybind_*) holding a comma separated list of keys.
struct KeyRow {
  const char* setting;
  const char* label;
};
constexpr KeyRow kKeyRows[] = {
    {"hid.keybind_a", "A"},
    {"hid.keybind_b", "B"},
    {"hid.keybind_x", "X"},
    {"hid.keybind_y", "Y"},
    {"hid.keybind_start", "Start"},
    {"hid.keybind_back", "Back"},
    {"hid.keybind_left_shoulder", "Left shoulder"},
    {"hid.keybind_right_shoulder", "Right shoulder"},
    {"hid.keybind_left_trigger", "Left trigger"},
    {"hid.keybind_right_trigger", "Right trigger"},
    {"hid.keybind_lstick_up", "Left stick up"},
    {"hid.keybind_lstick_down", "Left stick down"},
    {"hid.keybind_lstick_left", "Left stick left"},
    {"hid.keybind_lstick_right", "Left stick right"},
    {"hid.keybind_lstick_press", "Left stick press"},
    {"hid.keybind_rstick_up", "Right stick up"},
    {"hid.keybind_rstick_down", "Right stick down"},
    {"hid.keybind_rstick_left", "Right stick left"},
    {"hid.keybind_rstick_right", "Right stick right"},
    {"hid.keybind_rstick_press", "Right stick press"},
    {"hid.keybind_dpad_up", "D-pad up"},
    {"hid.keybind_dpad_down", "D-pad down"},
    {"hid.keybind_dpad_left", "D-pad left"},
    {"hid.keybind_dpad_right", "D-pad right"},
    {"hid.keybind_guide", "Guide"},
};

// The keys of a binding as the settings write them ("Space, Semicolon"), or a dash for none.
std::string KeysText(rex::hid::IBindingSet& bindings, size_t binding) {
  std::string text;
  for (const rex::InputSource& source : bindings.GetSources(binding)) {
    const std::string name = rex::InputSourceToString(source);
    if (!name.empty()) {
      text += (text.empty() ? "" : ", ") + name;
    }
  }
  return text.empty() ? "-" : text;
}

class OptionsDialog final : public rex::ui::DialogBase {
 public:
  explicit OptionsDialog(rex::ui::IUISystem& ui) : ui_(ui) {}

  void OnShow() override {
    busy_ = true;  // Ignore a B still held from the game.
    std::snprintf(gamertag_, sizeof(gamertag_), "%s", GetGamertag().c_str());
  }

  bool Draw(ImGuiIO& io) override {
    FeedGamepads(io);
    const float scale = ApplyUiScale(io);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520 * scale, 0), ImGuiCond_Appearing);
    // Esc, or B or Start on a controller, closes Options. B only when it is not backing out of
    // a list or a slider: last frame nothing was open or being changed. Start not with Back held:
    // Back + Start is the shortcut, which closes Options itself.
    // While a key binding waits for a key, every key belongs to it (Esc held cancels it).
    const bool capturing = capture_ != nullptr;
    // The SDK holds key presses back from the input system, where the capture reads them, while
    // ImGui wants the keyboard. Keyboard navigation claims it, so it lets go while a key is
    // awaited (the rows are disabled then, so a key cannot press anything here either).
    io.ConfigNavCaptureKeyboard = !capturing;
    bool open = capturing ||
                (!ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
                 !(ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) &&
                   !ImGui::IsKeyDown(ImGuiKey_GamepadBack)) &&
                 !(ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) && !busy_));
    if (ImGui::Begin("Options##ncaabb10", &open, ImGuiWindowFlags_NoCollapse)) {
      if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        Checkbox("Fullscreen (Alt+Enter, F11)", "ui.fullscreen");
        Checkbox("Exclusive fullscreen", "ui.fullscreen_exclusive",
                 "Changes the display mode instead of using a borderless window");
        static const char* const ratio_labels[] = {"Stretch", "16:9", "16:10", "21:9", "4:3"};
        Combo("Aspect ratio", "ui.aspect_ratio", ratio_labels, kAspectRatios);
        static const char* const scale_labels[] = {"1x (720p)", "2x (1440p)", "3x (2160p)"};
        static const char* const scale_values[] = {"1", "2", "3"};
        const std::string scale = GetShown("gpu.resolution_scale_x");
        int scale_index = scale == "3" ? 2 : scale == "2" ? 1 : 0;
        if (ImGui::Combo("Render resolution *", &scale_index, scale_labels, 3)) {
          SetAndSave("gpu.resolution_scale_x", scale_values[scale_index]);
          SetAndSave("gpu.resolution_scale_y", scale_values[scale_index]);
        }
        ImGui::SetItemTooltip("The game draws at 720p, as on the console. 2x held 60 fps in game on\n"
                              "an Arc A750; 3x dropped to about 40 there. The first start after a\n"
                              "change takes a few seconds longer while the shaders are rebuilt.");
        static const char* const aa_labels[] = {"Off", "FXAA", "FXAA, stronger"};
        static const char* const aa_values[] = {"none", "fxaa", "fxaa_extreme"};
        Combo("Anti-aliasing *", "gpu.xenos.swap_post_effect", aa_labels, aa_values);
        ImGui::SetItemTooltip("Smooths jagged edges on the final picture. No measurable cost.");
      }
      if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen)) {
        Combo("Texture filtering", "gpu.anisotropic_override", kAnisoLabels, kAnisoValues);
        Checkbox("Wait for display vsync", "gpu.vsync",
                 "Off is smoother: the game paces itself at 60 fps");
        // Each renderer has its own setting; "any" is D3D12 on Windows and Vulkan elsewhere.
        const std::string driver = GetShown("gpu.driver");
#if defined(_WIN32)
        // No renderer choice here: Vulkan on Windows draws the game right but at about 30 fps
        // in game on the A750, against 60 on D3D12 (gpu.driver=vulkan still selects it).
        const bool vulkan = driver == "vulkan";
#else
        const bool vulkan = driver == "vulkan" || driver == "any";
#endif
        if (vulkan) {
          static const char* const path_labels[] = {"Fast (host render targets)",
                                                    "Accurate (fragment shader interlock)"};
          static const char* const path_values[] = {"", "fsi"};
          Combo("Render path *", "gpu.vulkan.render_target_path", path_labels, path_values);
        } else if (driver != "null") {
          static const char* const path_labels[] = {"Fast (host render targets)",
                                                    "Accurate (rasterizer ordered views)"};
          static const char* const path_values[] = {"rtv", "rov"};
          Combo("Render path *", "gpu.d3d12.render_target_path", path_labels, path_values);
        }
      }
      if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        Slider("Volume", "apu.gain", 0.0f, 2.0f, "%.2f");
        Checkbox("Mute", "apu.mute");
        Checkbox("Mute when the window is in the background", "apu.mute_unfocused");
      }
      if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::string pads = GamepadNames();
        if (pads.empty()) {
          ImGui::TextColored(ImVec4(1, 0.65f, 0.3f, 1), "No controller found. Plug one in at any time.");
        } else {
          ImGui::TextWrapped("Controller: %s", pads.c_str());
        }
        Checkbox("Keyboard as controller", "hid.mnk_mode",
                 "The keyboard is player 1's controller, alongside any pad.\n"
                 "Return Start, Space A, Backspace B, L X, P Y, Tab Back\n"
                 "W/A/S/D left stick, arrows right stick, Shift+arrows D-pad\n"
                 "1/3 shoulders, Q/E triggers");
        Checkbox("Vibration", "hid.vibration");
        Slider("Left stick deadzone", "hid.left_stick_deadzone", 0.0f, 0.5f, "%.2f");
        Slider("Right stick deadzone", "hid.right_stick_deadzone", 0.0f, 0.5f, "%.2f");
      }
      if (GetShown("hid.mnk_mode") == "true" && g_input &&
          ImGui::CollapsingHeader("Keyboard controls")) {
        DrawKeyBindings(scale);
      }
      if (g_registry && g_registry->Find("kernel.gamertag") &&
          ImGui::CollapsingHeader("Profile", ImGuiTreeNodeFlags_DefaultOpen)) {
        // The name the game shows on its menus and save screens, as a gamertag would be. Letters,
        // digits and spaces: the game's fonts have them all.
        ImGui::InputText("Player name *", gamertag_, sizeof(gamertag_),
                         ImGuiInputTextFlags_CallbackCharFilter,
                         [](ImGuiInputTextCallbackData* data) {
                           const ImWchar c = data->EventChar;
                           return (c < 128 && (std::isalnum(static_cast<int>(c)) || c == ' ')) ? 0 : 1;
                         });
        // A on a controller starts typing (ImGui's own way is Y, which the menus leave out).
        if (ImGui::IsItemFocused() && !ImGui::IsItemActive() &&
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
          ImGui::SetKeyboardFocusHere(-1);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          std::string name = gamertag_;
          name.erase(0, name.find_first_not_of(' '));
          name.erase(name.find_last_not_of(' ') + 1);
          SetAndSave("kernel.gamertag", name.empty() ? "Player" : name);
          std::snprintf(gamertag_, sizeof(gamertag_), "%s", GetGamertag().c_str());
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Shown on the game's menus and save screens. Up to 15 letters, digits\n"
                            "and spaces. Takes effect when the game starts; once it runs, on the\n"
                            "next start.");
        }
      }
      if (ImGui::CollapsingHeader("Launcher")) {
        bool show = Settings().show_launcher.Get();
        if (ImGui::Checkbox("Show the launcher at startup", &show)) {
          SetAndSave("ncaabb10.show_launcher", show ? "true" : "false");
        }
      }
      ImGui::Separator();
      ImGui::TextDisabled("Saved to %s. * applies after a restart.",
                          Utf8(OptionsFile().filename()).c_str());
      if (ImGui::Button("All settings (F4)")) {
        ui_.ToggleOverlay("settings");
      }
      ImGui::SameLine();
      if (ImGui::Button("Close")) {
        open = false;
      }
      ImGui::TextDisabled("Controller: D-pad to move, A to choose, B to go back or close");
    }
    busy_ = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || ImGui::IsAnyItemActive() ||
            ImGui::IsKeyDown(ImGuiKey_GamepadFaceRight) || capture_ != nullptr;
    ImGui::End();
    if (!open) {
      capture_.reset();  // A capture must not outlive the input system.
      capturing_ = -1;
      // Close through the overlay registry rather than by returning false: the registry then
      // releases the input block Options holds (SDK 0.11.0 keeps it after a self-close).
      ui_.ShowOverlay("options", false);
    }
    return true;
  }

  bool WantsExclusiveInput() const override { return true; }

 private:
  // One row per pad input: its keys and a Change button that takes the next key pressed.
  void DrawKeyBindings(float scale) {
    rex::hid::IBindingSet& bindings = g_input->GetKeyBindings();
    if (capture_) {
      if (const auto source = capture_->Poll()) {
        Rebind(bindings, capturing_, *source);
        capture_.reset();
        capturing_ = -1;
      } else if (capture_->WasCanceled()) {
        key_note_ = "Canceled; the keys stay as they were.";
        capture_.reset();
        capturing_ = -1;
      }
    }
    ImGui::TextDisabled("Change, then press the new key. Hold Esc to cancel.");
    // Nothing can be pressed while a key is awaited: Enter or Space would press a button too.
    ImGui::BeginDisabled(capture_ != nullptr);
    if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn("input", ImGuiTableColumnFlags_WidthFixed, 130 * scale);
      ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("change", ImGuiTableColumnFlags_WidthFixed);
      for (int row = 0; row < static_cast<int>(std::size(kKeyRows)); ++row) {
        const auto binding = bindings.FindBySetting(kKeyRows[row].setting);
        if (!binding) {
          continue;
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(kKeyRows[row].label);
        ImGui::TableNextColumn();
        if (row == capturing_) {
          ImGui::EndDisabled();
          ImGui::TextColored(kTitleColorOptions, "Press a key...");
          ImGui::BeginDisabled(true);
        } else {
          ImGui::TextUnformatted(KeysText(bindings, *binding).c_str());
        }
        ImGui::TableNextColumn();
        ImGui::PushID(row);
        if (ImGui::SmallButton("Change")) {
          capture_ = g_input->CreateKeyCapture({});
          capture_->Begin();
          capturing_ = row;
          key_note_.clear();
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (ImGui::Button("Reset all keys")) {
      // Back to the defaults, and out of the options file, which keeps only what was changed.
      for (const KeyRow& row : kKeyRows) {
        if (const auto binding = bindings.FindBySetting(row.setting)) {
          (void)bindings.Reset(*binding);
        }
        g_user_values.erase(row.setting);
        g_applied.erase(row.setting);
      }
      Save();
      key_note_ = "All keys are back to their defaults.";
    }
    ImGui::EndDisabled();
    if (!key_note_.empty()) {
      TextDimWrapped(key_note_);
    }
  }

  // The new key replaces the input's keys. A key another input already uses stays there too, and
  // the note says so.
  void Rebind(rex::hid::IBindingSet& bindings, int row, const rex::InputSource& source) {
    const auto binding = bindings.FindBySetting(kKeyRows[row].setting);
    const std::string name = rex::InputSourceToString(source);
    if (!binding || name.empty()) {
      key_note_ = "That key can't be used.";
      return;
    }
    if (!bindings.SetSources(*binding, std::span<const rex::InputSource>(&source, 1))) {
      key_note_ = "That key can't be used.";
      return;
    }
    SetAndSave(kKeyRows[row].setting, GetText(kKeyRows[row].setting));
    key_note_ = std::format("{} is now {}.", kKeyRows[row].label, name);
    if (const auto other = bindings.FindConflict(*binding, source)) {
      for (const KeyRow& candidate : kKeyRows) {
        if (bindings.FindBySetting(candidate.setting) == other) {
          key_note_ += std::format(" {} uses it too.", candidate.label);
        }
      }
    }
  }

  static constexpr ImVec4 kTitleColorOptions{0.55f, 0.95f, 0.45f, 1};

  rex::ui::IUISystem& ui_;
  bool busy_ = true;
  char gamertag_[16] = {};  // A gamertag is at most 15 characters.
  std::unique_ptr<rex::hid::IKeyCapture> capture_;
  int capturing_ = -1;  // The kKeyRows index awaiting a key.
  std::string key_note_;
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

// title.game_root as the title will use it: GetText keeps {app.root}-style references unresolved.
fs::path GameRootSetting() {
  return rex::detail::ResolveSettingPath(fs::path(GetText("title.game_root")));
}

// Paths compare by their parts: the setting renders with forward slashes, the launcher's folder
// with the platform's own.
bool SamePath(const fs::path& a, const fs::path& b) {
  return a.lexically_normal() == b.lexically_normal();
}

// Saves sit under user/<profile id>/<title id>/; any such folder means this copy has saves.
constexpr char kTitleIdFolder[] = "454108C3";

bool HasSaves(const fs::path& user_root) {
  std::error_code ec;
  for (const auto& profile : fs::directory_iterator(user_root, ec)) {
    std::error_code entry_ec;
    if (profile.is_directory(entry_ec) &&
        fs::is_directory(profile.path() / kTitleIdFolder, entry_ec)) {
      return true;
    }
  }
  return false;
}

// Takes the settings of another copy's options file into this one, as if chosen in Options: what
// can change now applies, what is fixed for this run applies on the next start. The game folder
// is left out; the launcher decides that.
void ImportOptions(const fs::path& file) {
  if (!g_registry) {
    return;
  }
  ReadOptions(file, [](const std::string& name, const std::string& value) {
    if (name == "title.game_root") {
      return;
    }
    const std::string text = TomlValueText(value);
    auto found = g_registry->Find(name);
    if (found && *found && (*found)->IsFrozen()) {
      if ((*found)->Check(text)) {
        g_next_start[name] = text;
        g_user_values[name] = value;
      }
      return;
    }
    if (auto set = g_registry->SetText(name, text); !set) {
      REXLOG_WARN("ncaabb10: imported option {} = {} refused: {}", name, value,
                  set.error().GetDescription());
      return;
    }
    g_user_values[name] = value;
    Remember(*g_registry, name);
  });
  Save();
}

struct Imported {
  int saves = 0;
  bool options = false;
  fs::path game;  // The other copy's game folder, when it is set up and verifies.
  std::string error;
};

// Brings an older copy's saves (its user/ folder, without the utility drive cache the game
// rebuilds) and options into this copy, which has no saves yet. picked is that copy's folder or
// its user/ folder. The older copy keeps everything; nothing here is overwritten.
Imported ImportOlderCopy(const fs::path& picked, const fs::path& user_root) {
  Imported result;
  std::error_code ec;
  fs::path root = picked;
  if (!fs::is_directory(root / "user", ec) && root.filename() == "user") {
    root = root.parent_path();
  }
  const fs::path old_user = root / "user";
  if (!HasSaves(old_user)) {
    result.error = "No saves in " + Utf8(picked) +
                   ". Choose the folder of the older copy, the one with ncaabb10.exe and a user "
                   "folder in it.";
    return result;
  }
  fs::create_directories(user_root, ec);
  if (fs::equivalent(old_user, user_root, ec)) {
    result.error = "That folder is this copy. Choose the folder of the older one.";
    return result;
  }
  // Each folder under <profile>/<title>/<content type>/ is one save; Headers holds their list
  // entries, not saves.
  for (const auto& profile : fs::directory_iterator(old_user, ec)) {
    for (const auto& type : fs::directory_iterator(profile.path() / kTitleIdFolder, ec)) {
      if (type.path().filename() == "Headers") {
        continue;
      }
      for (const auto& save : fs::directory_iterator(type.path(), ec)) {
        std::error_code save_ec;
        result.saves += save.is_directory(save_ec) ? 1 : 0;
      }
    }
  }
  std::vector<fs::path> copied;
  for (const auto& entry : fs::directory_iterator(old_user, ec)) {
    if (entry.path().filename() == "cache") {
      continue;
    }
    const fs::path to = user_root / entry.path().filename();
    std::error_code exists_ec;
    const bool was_there = fs::exists(to, exists_ec);
    std::error_code copy_ec;
    fs::copy(entry.path(), to, fs::copy_options::recursive | fs::copy_options::skip_existing,
             copy_ec);
    if (!was_there) {
      copied.push_back(to);
    }
    if (copy_ec) {
      // Take back what this copy did not have before, so a retry starts clean.
      for (const fs::path& path : copied) {
        std::error_code remove_ec;
        fs::remove_all(path, remove_ec);
      }
      result.error = "Could not copy the saves from " + Utf8(old_user) + ": " + copy_ec.message();
      result.saves = 0;
      return result;
    }
  }
  const fs::path old_options = root / "ncaabb10_options.toml";
  std::error_code options_ec;
  if (fs::is_regular_file(old_options, options_ec) &&
      !fs::equivalent(old_options, OptionsFile(), options_ec)) {
    ImportOptions(old_options);
    result.options = true;
  }
  if (CheckGameFolder(root / "game") == GameCheck::kOk && NativeText(root / "game")) {
    result.game = (root / "game").lexically_normal().make_preferred();
  }
  REXLOG_INFO("ncaabb10: brought over {} saves from {} (options {}, game {})", result.saves,
              Utf8(old_user), result.options, Utf8(result.game));
  return result;
}

// The launcher's backdrop: a dim outline of a college court (94 by 50 ft, the 2009 three-point
// arc at 20 ft 9 in) fitted to the window, over a dark gradient. Drawn behind every window, so
// Options opens over it too.
void DrawCourt(ImDrawList& draw, ImVec2 display, float scale) {
  draw.AddRectFilledMultiColor(ImVec2(0, 0), display, IM_COL32(4, 26, 6, 255),
                               IM_COL32(4, 26, 6, 255), IM_COL32(0, 4, 0, 255),
                               IM_COL32(0, 4, 0, 255));
  constexpr float kLength = 94, kWidth = 50, kPi = 3.14159265f;
  const float margin = 0.06f * std::min(display.x, display.y);
  const float feet = std::min((display.x - 2 * margin) / kLength, (display.y - 2 * margin) / kWidth);
  if (feet <= 0) {
    return;
  }
  const ImVec2 origin((display.x - kLength * feet) * 0.5f, (display.y - kWidth * feet) * 0.5f);
  const auto at = [&](float x, float y) { return ImVec2(origin.x + x * feet, origin.y + y * feet); };
  const ImU32 fill = IM_COL32(10, 40, 12, 90);
  const ImU32 line = IM_COL32(70, 170, 60, 60);
  const float thick = std::max(1.0f, 1.5f * scale);
  draw.AddRectFilled(at(0, 0), at(kLength, kWidth), fill);
  draw.AddRect(at(0, 0), at(kLength, kWidth), line, 0, 0, thick);
  draw.AddLine(at(47, 0), at(47, kWidth), line, thick);
  draw.AddCircle(at(47, 25), 6 * feet, line, 0, thick);
  draw.AddCircle(at(47, 25), 2 * feet, line, 0, thick);
  for (int end = 0; end < 2; ++end) {
    // x measured from this end's baseline.
    const auto x = [&](float from_baseline) { return end ? kLength - from_baseline : from_baseline; };
    const float facing = end ? kPi : 0;  // The arcs open toward mid-court.
    draw.AddRect(at(std::min(x(0), x(19)), 19), at(std::max(x(0), x(19)), 31), line, 0, 0, thick);
    draw.AddCircle(at(x(19), 25), 6 * feet, line, 0, thick);
    draw.PathArcTo(at(x(5.25f), 25), 20.75f * feet, facing - kPi / 2, facing + kPi / 2, 48);
    draw.PathStroke(line, 0, thick);
    draw.AddLine(at(x(0), 4.25f), at(x(5.25f), 4.25f), line, thick);
    draw.AddLine(at(x(0), 45.75f), at(x(5.25f), 45.75f), line, thick);
    draw.AddLine(at(x(4), 22), at(x(4), 28), line, thick);
    draw.AddCircle(at(x(5.25f), 25), 0.75f * feet, line, 0, thick);
  }
}

// A larger button in brighter colors, for the one thing the launcher expects next.
bool PrimaryButton(const char* label, ImVec2 size) {
  const ImVec4 hovered = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
  const auto lighter = [](ImVec4 c, float by) {
    return ImVec4(std::min(1.0f, c.x + by), std::min(1.0f, c.y + by), std::min(1.0f, c.z + by), 1);
  };
  ImGui::PushStyleColor(ImGuiCol_Button, hovered);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, lighter(hovered, 0.12f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, lighter(hovered, 0.2f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.02f, 0.1f, 0.02f, 1));  // Dark on bright reads best.
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return pressed;
}

constexpr ImVec4 kTitleColor(0.55f, 0.95f, 0.45f, 1);

// Text at a whole multiple of the pixel font's size, which keeps it crisp.
void BigText(const char* text, float times, const ImVec4& color) {
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * times);
  ImGui::TextColored(color, "%s", text);
  ImGui::PopFont();
}

class LauncherDialog final : public rex::ui::DialogBase {
 public:
  enum class Result { kNone, kPlay, kQuit };

  LauncherDialog(rex::ui::IUISystem& ui, fs::path folder, fs::path default_folder,
                 fs::path user_root)
      : ui_(ui),
        folder_(folder.lexically_normal().make_preferred()),
        default_folder_(default_folder.lexically_normal().make_preferred()),
        user_root_(std::move(user_root)) {
    Recheck();
    // Test hook, like NCAABB10_INPUT_FILE: run the disc image setup on this file as if it had
    // been chosen in the file dialog, which a test script cannot drive.
    if (const char* iso = std::getenv("NCAABB10_TEST_ISO"); iso && *iso) {
      StartSetup(fs::path(std::u8string(iso, iso + std::strlen(iso))));
    }
    // The same for "Bring over saves from an older copy".
    if (const char* older = std::getenv("NCAABB10_TEST_IMPORT"); older && *older) {
      ImportFrom(fs::path(std::u8string(older, older + std::strlen(older))));
    }
  }

  Result result() const { return result_; }
  const fs::path& folder() const { return folder_; }

  bool Draw(ImGuiIO& io) override {
    if (extractor_ && extractor_->Done()) {
      extractor_->Join();
      const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                           extract_start_).count();
      REXLOG_INFO("ncaabb10: disc setup {} after {:.0f} s ({} MB): {}",
                  extractor_->Failed() ? "failed" : "done", seconds,
                  extractor_->BytesDone() >> 20, extractor_->Status());
      if (extractor_->Canceled()) {
        message_ = "Setup canceled. Nothing was changed.";
        message_is_error_ = false;
      } else if (extractor_->Failed()) {
        message_ = extractor_->InstallMoved()
                       ? extractor_->Status()
                       : extractor_->Status() + " Nothing was changed; you can try again.";
        message_is_error_ = true;
        Recheck();  // The game folder may have lost its install: Play must not stay enabled.
      } else {
        folder_ = extract_to_;
        Recheck();
        message_ = "The game is set up. Press Play.";
        message_is_error_ = false;
        if (extractor_->LeftOld()) {
          fs::path old = extract_to_;
          old += ".old";
          message_ += " Some files of yours could not be moved out of the old game folder; they "
                      "are in " + Utf8(old) + ".";
        }
      }
      extractor_.reset();
    }

    FeedGamepads(io);
    // Arrow keys move between the controls and Enter or Space presses one, as the D-pad and A do.
    // Without claiming the keyboard: the SDK skips its hotkeys (F1, F11, Alt+Enter) for keys
    // ImGui claims.
    // Not while Alt is held: Alt+Enter toggles fullscreen, and ImGui would also take its Enter
    // as a press of the button under the cursor. Alt goes down before Enter, so the next frame's
    // navigation already sees it off.
    if (io.KeyAlt) {
      io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    } else {
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    io.ConfigNavCaptureKeyboard = false;
    const float scale = ApplyUiScale(io);
    DrawCourt(*ImGui::GetBackgroundDrawList(), io.DisplaySize, scale);
    const ImVec4 kGood(0.45f, 0.9f, 0.45f, 1), kWarn(1, 0.65f, 0.3f, 1);
    // The top stays put, centered for the usual height: a message appearing or the game becoming
    // playable grows the window downward instead of moving the buttons under the mouse.
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, std::max(8.0f, io.DisplaySize.y * 0.5f - 170 * scale)),
        ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowSize(ImVec2(std::min(660 * scale, io.DisplaySize.x - 16), 0),
                             ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * scale, 14 * scale));
    ImGui::Begin("NCAA Basketball 10##launcher", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    BigText("NCAA BASKETBALL 10", 3, kTitleColor);
    ImGui::TextDisabled("PC port %s (%s), recompiled with ReXGlue  |  built %s", NCAABB10_VERSION,
                        NCAABB10_GIT, __DATE__);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    // Whether keys are for the launcher: not while another window (Options, the F4 settings) has
    // focus over it; Esc there closes that window instead.
    const bool keys = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                      !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);

    if (extractor_) {
      DrawSetupProgress(keys, scale);
      ImGui::End();
      return true;
    }

    const bool ok = check_ == GameCheck::kOk;
    switch (check_) {
      case GameCheck::kOk:
        DrawStatus(kGood, "Ready to play", "Game files found and verified.", scale);
        break;
      case GameCheck::kWrongVersion:
        DrawStatus(kWarn, "Different version of the game",
                   "This port needs the USA disc (Title ID 454108C3) without a title update.",
                   scale);
        break;
      case GameCheck::kMissing:
        DrawStatus(kWarn, "Game not set up yet",
                   "Set it up from your disc image, or choose a folder you extracted it into.",
                   scale);
        break;
    }
    if (!message_.empty()) {
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, message_is_error_ ? kWarn : kGood);
      ImGui::TextWrapped("%s", message_.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::Spacing();

    // No mouse is needed: the arrow keys or the D-pad move the cursor, which starts on the large
    // button (Play, or disc setup when the game is not set up), Enter, Space or A press the button
    // under it, and Esc quits. Enter or A with no button under the cursor (after a mouse click
    // elsewhere) plays. Not with Alt held: Alt+Enter toggles fullscreen.
    const bool play_key =
        keys && !ImGui::IsAnyItemFocused() &&
        ((!io.KeyAlt && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) ||
         ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false));
    if (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      result_ = Result::kQuit;
    }
    // Put the controller cursor on the default button when the launcher opens, when the game
    // becomes playable and when a controller connects. Over a few frames: ImGui applies a focus
    // request on the next frame, and the first frames of a new window can drop it.
    const std::string pads = GamepadNames();
    if (ImGui::IsWindowAppearing() || ok != was_ok_ || (!pads.empty() && !had_pad_)) {
      focus_frames_ = 3;
    }
    was_ok_ = ok;
    had_pad_ = !pads.empty();
    // Only while the launcher has the keys: a controller plugged in with Options open over the
    // launcher must not pull the cursor back behind it.
    const bool appearing = keys && focus_frames_ > 0;
    if (focus_frames_ > 0) {
      --focus_frames_;
    }
    // The large button is the step the player needs next: Play, or setting the game up.
    const float button_height = 48 * scale;
    const float side_width = 130 * scale;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    // At least the width of its label, should the window be narrower than the launcher.
    const float primary_width =
        std::max(ImGui::GetContentRegionAvail().x - 2 * (side_width + gap), 160 * scale);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2);
    const bool primary = PrimaryButton(ok ? "Play" : "Set up from disc image...",
                                       ImVec2(primary_width, button_height));
    ImGui::PopFont();
    if (!ok) {
      ImGui::SetItemTooltip("Choose your disc image (.iso). The game (6.2 GB) is copied\ninto %s",
                            Utf8(default_folder_).c_str());
    }
    if (ok) {
      if (primary || play_key) {
        REXLOG_INFO("ncaabb10: launcher Play ({}; enter {} keypad {} pad A {} alt {})",
                    play_key ? "key" : "button", ImGui::IsKeyDown(ImGuiKey_Enter),
                    ImGui::IsKeyDown(ImGuiKey_KeypadEnter),
                    ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown), io.KeyAlt);
        result_ = Result::kPlay;
      }
    } else if (primary) {
      PickIso();
    }
    if (appearing) {
      ImGui::SetKeyboardFocusHere(-1);
      ImGui::SetNavCursorVisible(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Options", ImVec2(side_width, button_height))) {
      ui_.ToggleOverlay("options");
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit", ImVec2(side_width, button_height))) {
      result_ = Result::kQuit;
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextDisabled("GAME FILES, FROM YOUR OWN DISC");
    const ImVec2 small(0, 24 * scale);
    if (ok) {
      if (ImGui::Button("Set up again from a disc image...", small)) {
        PickIso();
      }
      ImGui::SetItemTooltip("Copy the game from your disc image again. The current copy\n"
                            "stays until the new one checks out.\nInto %s",
                            Utf8(default_folder_).c_str());
      ImGui::SameLine();
    }
    if (ImGui::Button("Use an extracted folder...", small)) {
      PickFolder();
    }
    ImGui::SetItemTooltip("Play from a folder you extracted the disc into yourself\n"
                          "(the one with default.xex in it).");
    ImGui::SameLine();
    // Only a folder that is there: Windows answers opening a missing one with an error box.
    ImGui::BeginDisabled(!folder_exists_);
    if (ImGui::Button("Open game folder", small)) {
      OpenFolder(folder_);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(folder_exists_ ? "Show the game folder in your file manager."
                                         : "The game folder does not exist yet.");
    // A new copy (a newer version unpacked into a folder of its own) starts without saves.
    if (!has_saves_) {
      ImGui::Spacing();
      ImGui::TextDisabled("SAVES");
      if (ImGui::Button("Bring over saves from an older copy...", small)) {
        PickOlderCopy();
      }
      ImGui::SetItemTooltip("Copy the saves and options of an earlier copy of this port\n"
                            "(choose its folder, the one with ncaabb10.exe). That copy keeps\n"
                            "them. If its game is set up, Play uses that one too.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    // Controls, then the controller and the launcher setting.
    ImGui::TextDisabled("Arrows/D-pad: move   Enter/A: choose   Esc: quit   "
                        "F1/Guide: Options   F11: fullscreen");
    if (pads.empty()) {
      ImGui::TextDisabled("No controller found: the keyboard works as one (see Options).");
    } else {
      ImGui::TextColored(kGood, "Controller: %s", pads.c_str());
    }
    bool show = Settings().show_launcher.Get();
    if (ImGui::Checkbox("Show this launcher at startup", &show)) {
      SetAndSave("ncaabb10.show_launcher", show ? "true" : "false");
    }
    ImGui::SetItemTooltip("When off, the game starts directly. Hold Shift while starting\n"
                          "to see the launcher again.");
    if (!show) {
      ImGui::SameLine();
      ImGui::TextDisabled("(hold Shift at startup to see it again)");
    }
    ImGui::End();
    if (result_ != Result::kNone) {
      // Keyboard navigation is the launcher's: in game the keyboard is the player's controller.
      io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
      io.ConfigNavCaptureKeyboard = true;
    }
    return result_ == Result::kNone;
  }

  bool WantsExclusiveInput() const override { return true; }

 private:
  // Once per change, not per frame: both touch the disk.
  void Recheck() {
    check_ = CheckGameFolder(folder_);
    std::error_code ec;
    folder_exists_ = !folder_.empty() && fs::is_directory(folder_, ec);
    has_saves_ = HasSaves(user_root_);
  }

  // The game's state as a card: a bar in the state's color, a headline, what it means and the
  // game folder. The card's fill goes on a channel under the text, which is laid out first.
  void DrawStatus(const ImVec4& color, const char* headline, const char* detail, float scale) {
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float pad = 10 * scale, bar = 4 * scale;
    draw.ChannelsSplit(2);
    draw.ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(ImVec2(start.x + bar + pad, start.y + pad));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(start.x + width - pad - ImGui::GetWindowPos().x);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2);
    ImGui::TextColored(color, "%s", headline);
    ImGui::PopFont();
    ImGui::TextWrapped("%s", detail);
    TextDimWrapped("Game folder: " + (folder_.empty() ? std::string("(none)") : Utf8(folder_)));
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    const float bottom = ImGui::GetItemRectMax().y + pad;
    draw.ChannelsSetCurrent(0);
    const ImVec2 end(start.x + width, bottom);
    draw.AddRectFilled(start, end, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.04f)), 3 * scale);
    draw.AddRectFilled(start, ImVec2(start.x + bar, bottom), ImGui::GetColorU32(color), 3 * scale,
                       ImDrawFlags_RoundCornersLeft);
    draw.ChannelsMerge();
    // Past the card: Dummy so the window grows to hold the bottom padding.
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(width, bottom - start.y));
  }

  void DrawSetupProgress(bool keys, float scale) {
    BigText("Setting up the game", 2, kTitleColor);
    ImGui::TextUnformatted("Copying the game files from your disc image...");
    ImGui::ProgressBar(extractor_->Progress(), ImVec2(-1, 0));
    const double done = static_cast<double>(extractor_->BytesDone());
    const double total = static_cast<double>(extractor_->BytesTotal());
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - extract_start_).count();
    char line[160];
    int n = std::snprintf(line, sizeof(line), "%.1f of %.1f GB", done / 1e9, total / 1e9);
    // Speed and time left once there is enough to go on.
    if (seconds > 3 && done > 0 && total > done) {
      const double rate = done / seconds;
      const int left = static_cast<int>((total - done) / rate) + 1;
      n += std::snprintf(line + n, sizeof(line) - n, "   %.0f MB/s", rate / 1e6);
      if (left >= 60) {
        std::snprintf(line + n, sizeof(line) - n, "   about %d min %02d s left", left / 60, left % 60);
      } else {
        std::snprintf(line + n, sizeof(line) - n, "   about %d s left", left);
      }
    }
    ImGui::TextUnformatted(line);
    TextDimWrapped(extractor_->Status());
    TextDimWrapped("Into " + Utf8(extract_to_));
    ImGui::Spacing();
    // The cursor goes to Cancel, so A presses it; B and Esc cancel too.
    const bool cancel_key = keys && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                     ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false));
    if (ImGui::Button("Cancel (Esc / B)", ImVec2(160 * scale, 0)) || cancel_key) {
      extractor_->Cancel();
    }
    if (keys && (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemFocused())) {
      ImGui::SetKeyboardFocusHere(-1);
      ImGui::SetNavCursorVisible(true);
    }
    if (extractor_->Canceling()) {
      ImGui::SameLine();
      ImGui::TextDisabled("Canceling...");
    }
    focus_frames_ = 3;  // Back on the default button once the setup ends.
  }

  static void OpenFolder(const fs::path& folder) {
#if defined(_WIN32)
    ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    // A file:// URL, every byte outside the unreserved set percent-encoded, so any folder name
    // works. SDL hands it to xdg-open directly, without a shell.
    std::string url = "file://";
    for (const unsigned char c : folder.native()) {
      if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
        url += static_cast<char>(c);
      } else {
        url += std::format("%{:02X}", c);
      }
    }
    if (!SDL_OpenURL(url.c_str())) {
      REXLOG_WARN("ncaabb10: could not open {}: {}", url, SDL_GetError());
    }
#endif
  }

  void PickIso() {
    rex::ui::FileDialogConfig config;
    config.mode = rex::ui::FileDialogConfig::Mode::kOpenFile;
    config.title = "Choose your NCAA Basketball 10 disc image";
    config.filters = {{"Xbox 360 disc image", "iso"}};
    auto chosen = ui_.ShowFileDialog(config);
    if (!chosen || chosen->empty()) {
      return;
    }
    StartSetup(chosen->front());
  }

  // The extractor checks the free space once it knows the game's size from the disc's file list.
  void StartSetup(const fs::path& iso) {
    extract_to_ = default_folder_;
    message_.clear();
    REXLOG_INFO("ncaabb10: disc setup from {} into {}", Utf8(iso), Utf8(extract_to_));
    extract_start_ = std::chrono::steady_clock::now();
    extractor_ = std::make_unique<DiscExtractor>();
    extractor_->Start(iso, extract_to_);
  }

  void PickFolder() {
    rex::ui::FileDialogConfig config;
    config.mode = rex::ui::FileDialogConfig::Mode::kOpenFolder;
    config.title = "Choose the folder that holds default.xex";
    auto chosen = ui_.ShowFileDialog(config);
    if (!chosen || chosen->empty()) {
      return;
    }
    fs::path picked = chosen->front();
    // Accept the folder that holds default.xex, or a folder one level above it.
    if (CheckGameFolder(picked) == GameCheck::kMissing) {
      std::error_code ec;
      for (const auto& child : fs::directory_iterator(picked, ec)) {
        std::error_code child_ec;
        if (child.is_directory(child_ec) && fs::is_regular_file(child.path() / "default.xex", child_ec)) {
          picked = child.path();
          break;
        }
      }
    }
    const GameCheck check = CheckGameFolder(picked);
    if (check == GameCheck::kMissing) {
      message_ = "No default.xex in " + Utf8(chosen->front()) +
                 ". Choose the folder the disc was extracted into.";
      message_is_error_ = true;
      return;
    }
    if (!NativeText(picked)) {
      message_ = "The game can't use " + Utf8(picked) +
                 " on this PC: its name has characters outside the system's language settings. "
                 "Move the game to a folder with a plain name and choose it again.";
      message_is_error_ = true;
      return;
    }
    folder_ = picked.lexically_normal().make_preferred();
    Recheck();
    if (check_ == GameCheck::kOk) {
      RememberFolder();
    }
    message_ = check_ == GameCheck::kOk ? "The game is set up. Press Play." : "";
    message_is_error_ = false;
  }

  void PickOlderCopy() {
    rex::ui::FileDialogConfig config;
    config.mode = rex::ui::FileDialogConfig::Mode::kOpenFolder;
    config.title = "Choose the folder of your older copy (with ncaabb10.exe in it)";
    auto chosen = ui_.ShowFileDialog(config);
    if (!chosen || chosen->empty()) {
      return;
    }
    ImportFrom(chosen->front());
  }

  void ImportFrom(const fs::path& picked) {
    const Imported imported = ImportOlderCopy(picked, user_root_);
    if (!imported.error.empty()) {
      message_ = imported.error;
      message_is_error_ = true;
      Recheck();
      return;
    }
    message_ = std::format("Brought over {} save{}{}.", imported.saves,
                           imported.saves == 1 ? "" : "s",
                           imported.options ? " and your options" : "");
    if (!imported.game.empty() && check_ != GameCheck::kOk) {
      folder_ = imported.game;
      RememberFolder();
      message_ += " Play uses the game files in " + Utf8(folder_) +
                  ", so keep that folder (or set the game up again here).";
    }
    message_is_error_ = false;
    Recheck();
  }

  // Saves a chosen game folder at once, so quitting before Play keeps the choice.
  void RememberFolder() {
    if (!SamePath(GameRootSetting(), folder_)) {
      if (auto text = NativeText(folder_)) {
        SetAndSave("title.game_root", *text);
      }
    }
  }

  rex::ui::IUISystem& ui_;
  fs::path folder_;
  fs::path default_folder_;
  fs::path user_root_;
  fs::path extract_to_;
  GameCheck check_ = GameCheck::kMissing;
  bool folder_exists_ = false;
  bool has_saves_ = true;
  std::string message_;
  bool message_is_error_ = false;
  bool was_ok_ = false;
  bool had_pad_ = false;
  int focus_frames_ = 0;
  std::unique_ptr<DiscExtractor> extractor_;
  std::chrono::steady_clock::time_point extract_start_;
  Result result_ = Result::kNone;
};

bool ShiftHeld() {
#if defined(_WIN32)
  return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#else
  // SDL learns the modifier state when the window gets keyboard focus (X11 FocusIn, the Wayland
  // keyboard enter event), which can come a few frames after the window opens; nothing has pumped
  // those events yet. Pumping only queues them, so the UI still sees every one. Wait up to a
  // quarter second for the focus: a compositor that never gives it reads as Shift not held.
  SDL_PumpEvents();
  for (int i = 0; i < 25 && !SDL_GetKeyboardFocus(); ++i) {
    SDL_Delay(10);
    SDL_PumpEvents();
  }
  const bool focused = SDL_GetKeyboardFocus() != nullptr;
  const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
  REXLOG_INFO("ncaabb10: shift check: keyboard focus {}, shift {}", focused, shift);
  return shift;
#endif
}

// For this session only: Save would also persist command line values such as a test's flags.
void UseFolderThisSession(const fs::path& folder) {
  if (!SamePath(GameRootSetting(), folder) && g_registry) {
    if (auto text = NativeText(folder)) {
      (void)g_registry->SetText("title.game_root", *text);
    }
  }
}

}  // namespace

void Configure(rex::SettingsRegistry& settings) {
  g_registry = &settings;
  if (auto registered = settings.Register(Settings()); !registered) {
    REXLOG_WARN("ncaabb10: registering front end settings failed: {}",
                registered.error().GetDescription());
  }
  // The player's options (see LoadUserSettings for the layer they go in at).
  std::error_code ec;
  if (fs::exists(OptionsFile(), ec)) {
    LoadUserSettings(settings);
  }
}

#if !defined(_WIN32)
// The window icon. On Windows SDL takes it from the executable's icon resource.
void SetWindowIcon() {
#include "window_icon.inc"
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  if (!windows) {
    return;
  }
  SDL_Surface* icon = SDL_CreateSurfaceFrom(kWindowIconSize, kWindowIconSize,
                                            SDL_PIXELFORMAT_RGBA32,  // R, G, B, A bytes.
                                            const_cast<unsigned char*>(kWindowIcon),
                                            kWindowIconSize * 4);
  if (icon) {
    for (int i = 0; i < count; ++i) {
      if (!SDL_SetWindowIcon(windows[i], icon)) {
        REXLOG_WARN("ncaabb10: window icon: {}", SDL_GetError());
      }
    }
    SDL_DestroySurface(icon);
  }
  SDL_free(windows);
}
#endif

void InstallUI(rex::app::TitleApp& app) {
#if !defined(_WIN32)
  SetWindowIcon();
#endif
  g_input = &app.GetInput();
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
  g_pad_shortcuts.Start(ui);
}

void UninstallUI() {
  g_pad_shortcuts.Stop();
  g_input = nullptr;
  // Keeps what the player last chose in F4 for settings Options manages, for the next start.
  if (AdoptOtherChanges()) {
    Save();
  }
}

bool RunLauncher(rex::app::TitleApp& app) {
  // game/ sits beside user/. Made absolute and without a trailing separator first: parent_path()
  // of "user/" is "user", and of a relative "user" is empty.
  fs::path user_root = REX_PATHS().user_root.Get();
  std::error_code abs_ec;
  user_root = fs::absolute(user_root, abs_ec).lexically_normal();
  if (!user_root.has_filename()) {
    user_root = user_root.parent_path();
  }
  const fs::path default_folder = user_root.parent_path() / "game";
  fs::path folder = GameRootSetting();
  if (folder.empty() || CheckGameFolder(folder) == GameCheck::kMissing) {
    if (CheckGameFolder(default_folder) != GameCheck::kMissing) {
      folder = default_folder;
    }
  }
  const GameCheck check = CheckGameFolder(folder);
  const bool shift = ShiftHeld();
  REXLOG_INFO("ncaabb10: launcher: game folder '{}' check {} show_launcher {} shift {}", Utf8(folder),
              static_cast<int>(check), Settings().show_launcher.Get(), shift);
  if (check == GameCheck::kOk && !Settings().show_launcher.Get() && !shift) {
    UseFolderThisSession(folder);
    return true;
  }
  if (app.GetUI().IsHeadless()) {
    if (check == GameCheck::kOk) {
      UseFolderThisSession(folder);
      return true;
    }
    REXLOG_ERROR("ncaabb10: no verified game files; set title.game_root");
    return false;
  }

  auto launcher = std::make_unique<LauncherDialog>(app.GetUI(), folder, default_folder, user_root);
  LauncherDialog* view = launcher.get();
  rex::ScopeGuard shown = app.GetUI().AddDialog(*view);
  while (view->result() == LauncherDialog::Result::kNone && app.RunFrame()) {
  }
  REXLOG_INFO("ncaabb10: launcher closed: {}", view->result() == LauncherDialog::Result::kPlay   ? "play"
                                                  : view->result() == LauncherDialog::Result::kQuit ? "quit"
                                                                                                   : "window closed");
  if (view->result() != LauncherDialog::Result::kPlay) {
    return false;
  }
  if (!SamePath(GameRootSetting(), view->folder())) {
    if (auto text = NativeText(view->folder())) {
      SetAndSave("title.game_root", *text);
    }
  }
  return true;
}

}  // namespace ncaabb10::frontend
