// ncaabb10 - player-facing front end: launcher, Options overlay (F1), fullscreen hotkey.
#pragma once

#include <rex/app/title_app.h>
#include <rex/settings_registry.h>

namespace ncaabb10::frontend {

/** Registers the front end's own settings (ncaabb10.*). Call from OnConfigure. */
void Configure(rex::SettingsRegistry& settings);

/** Adds the Options overlay and the Alt+Enter / F11 fullscreen toggle. Call after Init. */
void InstallUI(rex::app::TitleApp& app);

/**
 * Shows the launcher until the player chooses Play (true) or quits (false). Skipped, returning
 * true, when verified game files are set up and ncaabb10.show_launcher is off, unless Shift is
 * held at startup. Sets title.game_root before returning true.
 */
bool RunLauncher(rex::app::TitleApp& app);

}  // namespace ncaabb10::frontend
