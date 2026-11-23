# ncaabb10-recomp

Static recompilation of **NCAA Basketball 10 (Xbox 360, USA)** with [ReXGlue](https://github.com/rexglue/rexglue-sdk) 0.11.0,
targeting Windows and Linux. No game files or code generated from them are in this repository:
you supply your own disc.

Status (Windows, Direct3D 12 Xenos backend, patched SDK): a full Play Now game plays from tip-off
to the post-game menu in a Release build; menus, profile, saves and the in-game broadcast overlays
work. See `../NCAA_BB10_RECOMP_PLAN.md` for the full plan.

## The game copy

Only this executable is supported (the analysis config is tied to it):

| Field | Value |
| --- | --- |
| Title ID | `454108C3` |
| Media ID | `78162526` |
| Version | `0.0.0.1` |
| `default.xex` SHA-256 | `3201c28392f73af9c969a740181996e954d3c894a7117eb81f8ff50362de79ef` |

The launcher sets the game up for you: start `ncaabb10.exe`, choose **Set up from a disc image
(.iso)** and it copies the game into `game/` next to the build's `user/` folder (about 7 GB), checks
`default.xex` and enables Play. **Use an extracted folder** accepts a folder you extracted yourself
(the folder with `default.xex`, or the one above it). The copy goes into `game.partial` first and replaces `game/`
only once `default.xex` verifies, so Cancel (or Esc) leaves an existing setup untouched. For development, extracting into `assets/`
works too:

```
python -I tools/xiso.py extract "NCAA Basketball 10 (USA).iso" assets
```

## Playing

| Key | Controller | Launcher | In game |
| --- | --- | --- | --- |
| Enter | A | Play (or the highlighted button) | (game) |
| | D-pad, left stick | Move between buttons | (game) |
| Esc | | Quit | (game) |
| F1 | Guide, or hold Back and press Start | Options | Options |
| Esc | B or Start | Close Options | Close Options |
| F11, Alt+Enter | | Fullscreen | Fullscreen |
| F4 | | All SDK settings | All SDK settings |

Any XInput, PlayStation, Switch Pro or other controller SDL knows works, and can be plugged in at
any time; the launcher and Options name the connected ones. In Options a controller moves with the
D-pad, A opens a list or toggles a box, B backs out of a list and then closes Options. A button
that opens or closes Options never reaches the game. Windows' Game Bar also opens on the Xbox
button unless it is turned off in Windows settings; Back + Start avoids it. For a rare controller
SDL lacks, put the community `gamecontrollerdb.txt` next to the executable.

The keyboard is also player 1's controller, alongside any pad (Options, "Keyboard as controller"):

| Keys | Controller |
| --- | --- |
| Return, X | Start |
| Space, `;` | A |
| Backspace, `'` | B |
| L | X |
| P | Y |
| Tab, Z | Back |
| W A S D | Left stick |
| Arrows | Right stick |
| Shift + arrows | D-pad |
| 1, 3 | Left, right shoulder |
| Q, E | Left, right trigger |
| F, K | Left, right stick press |

Options (F1) covers display (fullscreen, aspect ratio, render scale), graphics (texture filtering,
vsync, render path), audio (volume, mute) and controls (keyboard as pad, vibration, deadzones).
Changes apply at once and are saved to `ncaabb10_options.toml` beside the executable; settings
marked * apply after a restart. Untick "Show this launcher at startup" to go straight to the game;
hold Shift while starting to see the launcher again.

## Building on Windows

Needs Visual Studio 2022 or its Build Tools (MSVC + Windows SDK), CMake 3.25+, Ninja, a clang++ on
PATH (tested with clang 22) and the ReXGlue SDK 0.11.0 at the path in `CMakePresets.json`.
The preset makes clang target `x86_64-pc-windows-msvc` and link with `link.exe`, because the SDK
binaries use the MSVC ABI; `build_win.cmd` sets up the Visual Studio environment first.

```
build_win.cmd release
run_win.cmd release
```

Saves go to `out/build/win-amd64/<config>/user/`; the console's utility
drive (`cache:`) is backed by `user/cache/`.

Codegen runs as part of the build (about 2 minutes, 50k functions, ~270 MB of C++); the first
full compile takes about 5 minutes on 14 jobs.

## Patched SDK (recommended for Release)

Three patches fix SDK 0.11.0 bugs that this game hits:

- `patches/rexglue-sdk-0.11.0-texture-watch-race.patch`: the texture cache could stop watching a
  texture the CPU was still filling, leaving the broadcast text (score bar, lineups, rankings)
  blank in about 1 game in 4.
- `patches/rexglue-sdk-0.11.0-physical-heap-release-race.patch`: freeing physical memory on one
  thread could clear an allocation another thread had just made in the same pages; the game's
  next disc read into it failed and it froze on a dirty-disc error while loading a game.
- `patches/rexglue-sdk-0.11.0-overlay-self-close.patch`: an overlay closed with its own Close
  button or Esc stayed counted as visible, keeping its input block (the project's Options overlay
  also works around this, so the stock SDK is fine for it).

To build with them:

```
git clone --branch v0.11.0 --recurse-submodules https://github.com/rexglue/rexglue-sdk.git ..\rexglue-sdk-src
git -C ..\rexglue-sdk-src apply ..\ncaabb10-recomp\patches\rexglue-sdk-0.11.0-texture-watch-race.patch
git -C ..\rexglue-sdk-src apply ..\ncaabb10-recomp\patches\rexglue-sdk-0.11.0-physical-heap-release-race.patch
git -C ..\rexglue-sdk-src apply ..\ncaabb10-recomp\patches\rexglue-sdk-0.11.0-overlay-self-close.patch
tools\build_vulkan_deps.cmd
tools\build_sdk.cmd
build_win.cmd release 12 win-amd64-patched
```

`tools\build_vulkan_deps.cmd` needs `..\vulkan-deps` with Vulkan-Headers and Vulkan-Loader at
`v1.4.363` and VulkanMemoryAllocator at `v3.3.0` (or set `VULKAN_SDK` to a LunarG SDK instead).
The patched SDK is Release only, so use the stock preset for Debug builds (which still have these
bugs).

`src/diagnostics.cpp` logs the game's disc-error callback and any failed or slow file read (with
the guest page state of the buffer); set `NCAABB10_TRACE_PHYS=1` to also log physical allocations
and frees.

## Performance

Live play holds 60 fps on an Intel Arc A750 (Release, patched SDK). Two project defaults matter:

- `gpu.d3d12.render_target_path` defaults to `rtv` (set in `src/main.cpp`). The SDK would pick
  `rov` on Intel GPUs, which runs this game at 12-20 fps. Override with
  `--gpu.d3d12.render_target_path=rov` if a scene renders wrong.
- `src/gpu_wait.cpp` makes the game's Direct3D GPU wait sleep instead of spinning a core
  (`NCAABB10_GPU_WAIT_SPIN=1` restores the spin).

Measuring: `NCAABB10_PERF=1` logs fps, frame times, CPU and the busiest threads every 5 s;
`NCAABB10_PROFILE=1` adds a sampling profile of guest functions every 10 s (it costs fps while
on); `tools/perf_run.ps1 -Out <folder>` runs a game and prints in-game fps and GPU use.

## Linux

Set `REXGLUE_SDK` to a Linux build of the SDK 0.11.0 and use the `linux-amd64` preset. Not tested yet.

## Automated testing

`tools/drive.ps1` starts the game, feeds it keys through a file (the game follows the file named by
`NCAABB10_INPUT_FILE` and injects the keys in process, so nothing goes to the desktop's focused
window), and screenshots the game window with PrintWindow:

```powershell
& tools\drive.ps1 -Exe out\build\win-amd64\Debug\ncaabb10.exe -OutDir out\shots `
  -GameArgs "--title.game_root=$PWD\assets --ui.fullscreen=false --hid.mnk_mode=true" `
  -Steps "wait:36","key:RETURN","wait:10","shot:after_start"
```

`NCAABB10_TEST_ISO=<path>` makes the launcher start the disc image setup on that file, as if
chosen in the file dialog (which a script cannot drive); the log reports the outcome.

A virtual SDL controller stands in for a real one: `pad:<BUTTON>[:<ms>]` presses a button
(A B X Y BACK GUIDE START LS RS LB RB UP DOWN LEFT RIGHT), `padhold:<BUTTON>` and `padup:<BUTTON>`
hold and release one, and `axis:<LX|LY|RX|RY|LT|RT>:<value>` sets a stick or trigger
(-32768 to 32767). It attaches on the first such step; `drive.ps1` sets
`SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` so it works while the window is in the background.

Steps also include `ui:<KEY>[+ALT]`, which pushes the key through the window's own event queue so
it reaches the launcher and the F1/F11/Alt+Enter binds, and `winsize:<name>`. For example, the
launcher check is
`-Steps "wait:8","ui:F1","wait:2","ui:ESCAPE","wait:2","ui:RETURN","wait:30","shot:title"`.

`tools/to_game.ps1 -Out <folder>` drives a Release build from boot into a Duke at North Carolina
game and screenshots it (it needs a profile save with autosave on in the build's `user` folder).

Steps are `watch:<seconds>:<interval>:<prefix>`, `wait:<seconds>`, `key:<System.Windows.Forms.Keys name>` and `shot:<name>`. Waits are
fixed, so leave margin: loads take 30-60 s in a Debug build.
