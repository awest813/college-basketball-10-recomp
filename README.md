# ncaabb10-recomp

Static recompilation of **NCAA Basketball 10 (Xbox 360, USA)** with [ReXGlue](https://github.com/rexglue/rexglue-sdk) 0.11.0,
targeting Windows and Linux. No game files or code generated from them are in this repository:
you supply your own disc.

Status (Windows, Direct3D 12 Xenos backend, Debug build): boots, takes input, runs the first-run
flow (favorite school, autosave prompt), renders the 3D campus court and the main menu, opens
Play Now team select, and saves/loads the profile across restarts. Not yet played: an actual game.
See `../NCAA_BB10_RECOMP_PLAN.md` for the full plan.

## The game copy

Only this executable is supported (the analysis config is tied to it):

| Field | Value |
| --- | --- |
| Title ID | `454108C3` |
| Media ID | `78162526` |
| Version | `0.0.0.1` |
| `default.xex` SHA-256 | `3201c28392f73af9c969a740181996e954d3c894a7117eb81f8ff50362de79ef` |

Extract the disc into `assets/`:

```
python -I tools/xiso.py extract "NCAA Basketball 10 (USA).iso" assets
```

## Building on Windows

Needs Visual Studio 2022 or its Build Tools (MSVC + Windows SDK), CMake 3.25+, Ninja, a clang++ on
PATH (tested with clang 22) and the ReXGlue SDK 0.11.0 at the path in `CMakePresets.json`.
The preset makes clang target `x86_64-pc-windows-msvc` and link with `link.exe`, because the SDK
binaries use the MSVC ABI; `build_win.cmd` sets up the Visual Studio environment first.

```
build_win.cmd debug
run_win.cmd debug
```

Keyboard as pad (`--hid.mnk_mode=true`): Return = Start, Space = A, Backspace = B, arrows = D-pad,
W/A/S/D = left stick. Saves go to `out/build/win-amd64/<config>/user/`; the console's utility
drive (`cache:`) is backed by `user/cache/`.

Codegen runs as part of the build (about 2 minutes, 50k functions, ~270 MB of C++); the first
full compile takes about 5 minutes on 14 jobs.

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

Steps are `wait:<seconds>`, `key:<System.Windows.Forms.Keys name>` and `shot:<name>`. Waits are
fixed, so leave margin: loads take 30-60 s in a Debug build.
