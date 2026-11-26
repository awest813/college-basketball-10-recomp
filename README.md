# ncaabb10-recomp

Static recompilation of **NCAA Basketball 10 (Xbox 360, USA)** with [ReXGlue](https://github.com/rexglue/rexglue-sdk) 0.11.0,
targeting Windows and Linux. No game files or code generated from them are in this repository:
you supply your own disc.

The repository's own code is under the MIT license ([LICENSE](LICENSE)). That covers neither the
game nor code generated from it, nor the ReXGlue SDK and its libraries, which keep their own
licenses (the SDK patches in `patches/` are offered under the SDK's BSD 3-Clause license). Not
affiliated with or endorsed by EA or the NCAA.

Status (Windows, Direct3D 12 Xenos backend, patched SDK): a full Play Now game plays from tip-off
to the post-game menu in a Release build; menus, profile, saves and the in-game broadcast overlays
work. Linux builds and runs too (see Linux below).

## The game copy

Only this executable is supported (the analysis config is tied to it):

| Field | Value |
| --- | --- |
| Title ID | `454108C3` |
| Media ID | `78162526` |
| Version | `0.0.0.1` |
| `default.xex` SHA-256 | `3201c28392f73af9c969a740181996e954d3c894a7117eb81f8ff50362de79ef` |

The launcher sets the game up for you: start `ncaabb10.exe`, choose **Set up from disc image**
(the large button until the game is set up; **Set up again from a disc image** below it after) and
pick your `.iso`; it copies the game into `game/` next to the build's `user/` folder (6.2 GB, about a
minute on an SSD; the launcher shows the speed and time left), checks `default.xex` and enables
Play. Setup checks for that much free space before it copies anything (the old `game/` stays until
the new copy verifies, so a reinstall needs the space again). **Use an extracted folder** accepts a folder you extracted yourself
(the folder with `default.xex`, or the one above it). The copy goes into `game.partial` first and
replaces `game/` only once `default.xex` verifies, so Cancel (Esc, or B) leaves an existing setup
untouched. A damaged or altered image (a looping or oversized file list, file names a real disc
never has, a short read) stops the setup before anything is written, and if swapping in the new
copy fails (a file held open by another program), the old `game/` is put back. Files of your own
in `game/` that the disc does not have, such as the disc image itself if you keep it there, move
into the new copy instead of being deleted with the old one. Game folders with names in any
language work: the Windows exe runs with UTF-8 as its code page (Windows 10 1903 or later).

Building from source needs the disc extracted into `assets/` as well: code generation reads
`assets/default.xex`, and `run_win.cmd` plays from that folder:

```
python -I tools/xiso.py extract "NCAA Basketball 10 (USA).iso" assets
```

## Playing

| Key | Controller | Launcher | In game |
| --- | --- | --- | --- |
| Enter, Space | A | Press the highlighted button (Play to start with) | (game) |
| Arrows | D-pad, left stick | Move between buttons | (game) |
| Esc | | Quit (or cancel disc setup) | (game) |
| | B | Cancel disc setup | (game) |
| F1 | Guide, or hold Back and press Start | Options | Options |
| Esc | B or Start | Close Options | Close Options |
| F11, Alt+Enter | | Fullscreen | Fullscreen |
| F4 | | All SDK settings | All SDK settings |

Any XInput, PlayStation, Switch Pro or other controller SDL knows works, and can be plugged in at
any time; the launcher and Options name the connected ones. In Options a controller moves with the
D-pad, A opens a list or toggles a box, B backs out of a list and then closes Options. A button
that opens or closes Options never reaches the game. Windows' Game Bar also opens on the Xbox
button unless it is turned off in Windows settings; Back + Start avoids it. For a rare controller
SDL lacks, put the community `gamecontrollerdb.txt` next to the executable (or, when that folder
is read only, beside `ncaabb10_options.toml` in the user settings folder).

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

Options (F1) has:
- **Display:** fullscreen, aspect ratio, render resolution, anti-aliasing (FXAA).
- **Graphics:** texture filtering, vsync, render path.
- **Audio:** volume, mute.
- **Controls:** keyboard as controller, vibration and stick deadzones. Under **Keyboard
  controls**, each pad input's key: **Change**, then press the new key (hold Esc to cancel);
  **Reset all keys** goes back to the table above.
- **Profile:** the player name the game shows on its menus and save screens (patched SDK).
- At the bottom: **Restart now** when a change waits for a restart, **Reset all...** (every
  option back to its default, keeping the game folder and player name, then a restart) and, in
  game, **Quit game...**. In game, restarting and quitting ask first, since progress since the
  last save is lost; a restart goes straight back into the game.

The game keeps running behind Options, so pause it first (Start). Changes apply at once and are
saved to `ncaabb10_options.toml` beside the executable. Settings marked * apply after a restart:
Options shows the new choice and lists them under "Restart to apply". The options file wins over
the same setting in `ncaabb10.toml`, which F4's "Save to config" writes; `REX_*` environment
variables and the command line win over both.

Untick "Show this launcher at startup" to go straight to the game; hold Shift while starting to
see the launcher again. On a large display (fullscreen at 1440p or 4K) the launcher and Options
scale up in whole steps, so they stay readable from a couch. The mouse cursor hides after 3
seconds without moving.

### Saves

The game saves through its own My NCAA > Load/Save screens and autosaves the profile when you
leave a settings screen. Everything goes under the build's `user/` folder:
`user/B13EBABEBABEBABE/454108C3/00000001/` holds one folder per save (`USERPROFILE_0 <date>`,
`ROSTERS_0 ..`, `DYNASTY_0 ..`), with the headers the save lists show in `Headers/`. Saves belong
to the profile's ID, not its name, so renaming the player keeps them. Checked: saving, overwriting,
loading and deleting the profile, a roster set and a Dynasty; the profile autosave; and loading a
Dynasty back to the same week. Online menus (Xbox Live, Dynamic Update Hub, ESPN on demand) say
you must be signed in to Xbox Live, as the console does offline.

### Updating to a newer version

The zip holds no `user/`, `game/`, `cache/` or options file, so unpacking a newer one over the
old folder (replace the files it asks about) keeps the saves, the options, the shader cache and
the game. Unpacked into a
folder of its own instead, the new copy starts without saves: its launcher then offers **Bring
over saves from an older copy**, which copies the old copy's saves and options (the old copy keeps
them) and, when the game is set up there, plays from that copy's `game/` folder. Choosing a game
folder (this way or with **Use an extracted folder**) is saved at once, not only on Play.

### First start on Windows

The exe is not code signed, so Windows SmartScreen may say it protected your PC from an
unrecognized app; **More info**, then **Run anyway**, starts it. The exe carries the port's version
(Properties > Details, and the line under the title in the launcher).

## Building on Windows

Needs:

- Visual Studio 2022 or later, any edition, or its Build Tools, with the **Desktop development
  with C++** workload (MSVC and the Windows SDK).
- clang 20 or newer from LLVM: `winget install LLVM.LLVM` (tested with clang 22). Visual Studio's
  own clang is 19, too old.
- CMake 3.25 or newer and Ninja: `winget install Kitware.CMake Ninja-build.Ninja`, or Visual
  Studio's **C++ CMake tools for Windows** component.
- Git, and Python 3 for the scripts in `tools/`.
- The disc extracted into `assets/` (see above).

From a fresh clone:

```
tools\setup_sdk.cmd
build_win.cmd
run_win.cmd
```

`tools\setup_sdk.cmd` runs once. It puts the patched SDK beside this folder (see Patched SDK below)
and builds it, which took about 20 minutes from an empty folder here. `build_win.cmd` builds the Release game into
`out\build\win-amd64-patched\Release`, and `run_win.cmd` starts it.

Each script stops early with a message that says what to do: when a tool is missing
(`tools\check_tools.cmd`, which also finds LLVM in its default folder when it is not on PATH), when
`assets\default.xex` or the SDK is missing, or when the SDK has no build of the configuration
asked for. The presets make clang target `x86_64-pc-windows-msvc` and link with `link.exe`,
because the SDK binaries use the MSVC ABI. `tools\vcvars.cmd` finds Visual Studio with vswhere.
The build uses every processor unless the second argument says otherwise
(`build_win.cmd release 8`).

`build_win.cmd debug` and `build_win.cmd relwithdebinfo` need the SDK built in that configuration
too (see Debugging below). `build_win.cmd debug 0 win-amd64` and `run_win.cmd stock debug` use the
stock SDK 0.11.0 instead, unpacked beside this folder as `rexglue-sdk-0.11.0-win-amd64/win-amd64`
from its [release page](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.11.0). It still has
the bugs the patches fix. Anything after the build words goes to the game unchanged, for example
`run_win.cmd --ncaabb10.show_launcher=true --hid.keybind_a=Space,J`.

Saves go to `out/build/win-amd64-patched/Release/user/` (see Saves above); the console's utility
drive (`cache:`) is backed by `user/cache/`.

Codegen runs as part of the build (50k functions, ~270 MB of C++). On a clean checkout here (20
threads) it took about 11 minutes and the compile about 8. Codegen runs again only when
`default.xex`, the manifest or the SDK changes; a change to the port's own code rebuilds in under
two minutes. `build_win.cmd` runs codegen on its own before the first build of a clean checkout,
because the build files only list the generated sources once they exist.

The build copies the Visual C++ runtime DLLs the exe and the SDK import (`vcruntime140*.dll`,
`msvcp140*.dll`) next to the exe, so the build folder runs on a PC without the VC++
redistributable. `tools/package.ps1` packs a Release build into `out/ncaabb10-windows-x64.zip`
(exe, DLLs, README, `LICENSE.txt`, `THIRD_PARTY_NOTICES.txt`; about 30 MB) without logs, saves, options or linker
leftovers. The notices file is built by `tools/notices.py` from the SDK source the build used
(`..\rexglue-sdk-src`, or `-Sdk <folder>`): the license of every library inside the shipped
binaries, and where the source of the two LGPL ones (FFmpeg in `rexkernel.dll`, libmspack in
`rexfilesystem.dll`) comes from. The port's version is in `CMakeLists.txt` (`project(... VERSION)`);
`tools/make_icon.py` draws the icon (`src/ncaabb10.ico`, `src/window_icon.inc`). Windows runs
no PowerShell scripts by default, so start this one and the `tools/*.ps1` test scripts as
`powershell -ExecutionPolicy Bypass -File tools\package.ps1`. It holds no game files: on
first start the launcher says the game is not set up and offers to set it up from a disc image or
an extracted folder.

## Patched SDK (recommended for Release)

Seven patches fix SDK 0.11.0 bugs that this game hits, or cut its cost:

- `patches/rexglue-sdk-0.11.0-texture-watch-race.patch`: the texture cache could stop watching a
  texture the CPU was still filling, leaving the broadcast text (score bar, lineups, rankings)
  blank in about 1 game in 4. (Fixed upstream the same way after 0.11.0, in `36cd208e`.)
- `patches/rexglue-sdk-0.11.0-physical-heap-release-race.patch`: freeing physical memory on one
  thread could clear an allocation another thread had just made in the same pages; the game's
  next disc read into it failed and it froze on a dirty-disc error while loading a game.
- `patches/rexglue-sdk-0.11.0-overlay-self-close.patch`: an overlay closed with its own Close
  button or Esc stayed counted as visible, keeping its input block (the project's Options overlay
  also works around this, so the stock SDK is fine for it).
- `patches/rexglue-sdk-0.11.0-profile-name.patch`: the profile was always named "User" and the
  storage device "Dummy HDD" on the game's menus and save screens. Adds `kernel.gamertag` (the
  project sets "Player"; Options changes it) and names the drives Hard Drive and Disc Drive.
- `patches/rexglue-sdk-0.11.0-register-write-lookup.patch`: every GPU register write looked the
  register up in a switch over the whole register table, only to warn about unknown ones; this
  was 3-6% of the GPU command thread. Checks a bitset built once instead.
- `patches/rexglue-sdk-0.11.0-dcbz-cache-block.patch`: the generated code for `dcbz` cleared 32
  bytes, but the Xenon cache block is 128 bytes and every `dcbz` in this game sits in a clear loop
  that steps 128 bytes (its memset among them), so 96 of every 128 bytes stayed uncleared
  (upstream issue #627, still open). Changes code generation, so the game build regenerates.
- `patches/rexglue-sdk-0.11.0-quiet-file-probes.patch`: every file the game looks for and does
  not find (about 70 optional `.db` and `dirinfo` files per load) was logged as a warning,
  burying real ones. Not found now logs at debug level; other failed opens still warn. A path on a
  device nothing is mounted at (`UPDATE:\`, `rmcsave:`, a debug dump device) and setting delete on
  close log at debug level too instead of as an error and a warning, and a missing file the game
  asks to create in a read only disc folder is refused (as before) without first a warning that
  it opens for read. Nothing the game gets back changes.

`tools\setup_sdk.cmd [jobs] [release|debug|relwithdebinfo|all]` sets them up beside this folder.
It skips any step that is already done, so after a failure fix the cause and run it again:

1. Clones the SDK at `v0.11.0` with its submodules into `..\rexglue-sdk-src`.
2. Applies each patch with `git apply`. It says "Applied already" for a patch that is in, and
   stops if one does not apply.
3. Shallow-clones Vulkan-Headers and Vulkan-Loader `v1.4.363` and VulkanMemoryAllocator `v3.3.0`
   into `..\vulkan-deps`, and builds them into `..\vulkan-deps\prefix`
   (`tools\build_vulkan_deps.cmd`). Skipped when `VULKAN_SDK` names a LunarG SDK.
4. Builds and installs the SDK into `..\rexglue-sdk-src\out\install\win-amd64`
   (`tools\build_sdk.cmd`). Release is the default.

After a change to the SDK source or to a patch, `tools\build_sdk.cmd` alone rebuilds it. Any of
Release, Debug and RelWithDebInfo can be installed side by side.

Upstream's development branch (nightly `44b7fdc5`, 2026-11-22, 66 commits after 0.11.0) was
tried with these patches and dropped. Its critical section handoff (`1a8d4199`, `ccc231e9`) leaves
the game's main thread spinning on a lock while the loader thread barely runs: boot took several
minutes. With those two commits reverted it boots, but it still stalls at 11-40 fps for about 30 s
during the intro, where 0.11.0 holds 60, and it had more long hitches over a 20 minute run.

`src/diagnostics.cpp` logs the game's disc-error callback and any failed or slow file read (with
the guest page state of the buffer); set `NCAABB10_TRACE_PHYS=1` to also log physical allocations
and frees.

A normal run logs no errors. The warnings left, all harmless: five `NtCreateFile failed
path='GAME:\dirinfo*' -> 0xc0000022` (the game asks to create these on the disc, which is read
only, and carries on), one `unimplemented: XLiveBaseUnk58046` (an Xbox Live call that the SDK
answers with success) and the occasional read over 50 ms that `src/diagnostics.cpp` reports. Each
game load also frees six small buffers twice in a row (from `826C066C`); the project's
`MmFreePhysicalMemory` hook skips a free of memory that is not allocated, which the SDK would refuse
anyway, and logs it at debug level instead of the SDK's two errors per free.

## Debugging

### Logs

- Each start writes `logs\ncaabb10_<run>.log` beside the exe. The port keeps the newest 50 MB of
  older runs; set `--log.folder_budget=<bytes>` to change that, or 0 to keep every one.
- The first `ncaabb10:` line names the build that wrote the log, for example
  `PC port 0.1.0, commit 9ef275f, Release build of Nov 25 2026`. The commit ends in `-dirty` when
  the build had uncommitted changes. The launcher shows the same commit under its title.
- `--log.level=debug` (or `trace`) logs more from everything.
  `--log.categories.<category>=debug` does it for one category: the column after the level letter
  in each line (`core`, `gpu`, `kernel`, `xam`, `hid`, ...).
- With a debugger attached, the log also goes to its output window.

### Crashes

A crash ends the log with a crash report and shows a dialog that says so. The report has the
exception, the registers, and a backtrace whose frames are `module+offset`. To name them:

```
python -I tools\symbolize.py out\build\win-amd64-patched\Release\logs\ncaabb10_321.log
```

It prints the report with each frame named, using llvm-symbolizer from LLVM:

- A recompiled game function comes out as `sub_<guest address>`.
- The port's and the SDK's own code come out with file and line.

A Release build keeps what this needs beside the exe: `ncaabb10.pdb`, with every recompiled
function's name and the port's code, and the SDK's Release PDBs. The SDK build installs them and
the game's build copies them over. With them the Release link takes about 7 s, and
`ncaabb10.pdb` is about 12 MB. They are not in the zip.

For a crash on someone else's PC, they send the log. `tools\package.ps1` writes
`ncaabb10-windows-x64-symbols.zip` beside each zip it packs: keep it with that build. Unpack both
zips into one folder and pass it as `--bin <folder>`. `NCAABB10_TEST_CRASH=1` crashes on purpose
in the game's first physical allocation, to check all of this end to end.

On Linux the binaries keep their symbols, and the script reads the report the same way. It needs
llvm-symbolizer (`sudo apt install llvm-20`), which `tools/fetch_toolchain.sh` does not fetch.
It has not been tried on a Linux crash yet.

### Debug builds

The SDK builds in Debug or RelWithDebInfo as well, and installs beside Release:

```
tools\build_sdk.cmd 0 debug
build_win.cmd debug
run_win.cmd debug
```

The first line takes about 11 minutes. `all` builds all three configurations.

- **Debug:** the port's code unoptimized, the recompiled code at `-O1` with line tables. The
  game build takes about 8 minutes. On the A750 it reached the title screen at 60 fps, at 32-42
  fps while the intros load. It was not measured in a game.
- **RelWithDebInfo:** optimized, with line tables everywhere. It runs close to Release, which
  makes it the one to use under a debugger.

Debug and RelWithDebInfo each build into their own folder under `out\build\win-amd64-patched\`, so
each keeps its own `user\` saves and options.

To debug in Visual Studio, open the exe with
`devenv /debugexe out\build\win-amd64-patched\Debug\ncaabb10.exe --title.game_root=<assets>`
(`run_win.cmd` passes the same arguments). WinDbg or VS Code's C++ debugger work the same way.

Generated code is in `generated\default\ncaabb10_recomp.*.cpp`. Each function is named
`sub_<guest address>`, and each PowerPC instruction is a comment above the C++ it became, so a
breakpoint can go on the game's own instructions.

### Environment variables

| Variable | Does |
| --- | --- |
| `NCAABB10_PERF=1` | Logs fps, frame times, CPU and the busiest threads every 5 s |
| `NCAABB10_PROFILE=1` or `callers` | Windows: a sampling profile of guest functions every 10 s (see Performance) |
| `NCAABB10_TRACE_PHYS=1` | Logs every physical memory allocation and free |
| `NCAABB10_AUDIO_DUMP=<file.wav>` | Records the 6-channel mix sent to the audio driver |
| `NCAABB10_GPU_WAIT_SPIN=1` | The game's GPU wait spins, as the SDK does, instead of sleeping |
| `NCAABB10_INPUT_FILE=<file>` | Follows the file for test input (see Automated testing) |
| `NCAABB10_TEST_ISO=<iso>` | Starts the disc image setup on that file |
| `NCAABB10_TEST_IMPORT=<folder>` | Starts **Bring over saves** from that folder |
| `NCAABB10_TEST_CRASH=1` | Crashes on purpose, to check the crash report |
| `NCAABB10_SKIP_LAUNCHER=1` | Skips the launcher once. Options sets it for the new process when it restarts a running game |

A restart from Options starts the new process with the same command line and environment, so a
test's `NCAABB10_INPUT_FILE` follows it: start that file over (empty it) once the restart is under
way, or the new process plays every line again.

Each is read only when set, so a normal run is not affected.

## Performance

Live play holds 60 fps on an Intel Arc A750 (Release, patched SDK). The `dcbz` patch matters for
long sessions: over 21 minute games the build without it averaged 53.9 fps with 19 five-second
windows holding a frame over 150 ms (up to 448 ms, more often as the game went on), and two runs
with it averaged 57.8 and 57.7 fps with no frame over 150 ms. A whole game with it (51 minutes,
through halftime and the final buzzer to the post-game menu) averaged 58.0 fps with no frame over
111 ms. The project's own defaults (`src/main.cpp`; the config file, environment and command line
override them) matter as well:

- `gpu.d3d12.render_target_path` defaults to `rtv` (set in `src/main.cpp`). The SDK would pick
  `rov` on Intel GPUs, which runs this game at 12-20 fps. Override with
  `--gpu.d3d12.render_target_path=rov` if a scene renders wrong.
- `src/gpu_wait.cpp` makes the game's Direct3D GPU wait sleep instead of spinning a core
  (`NCAABB10_GPU_WAIT_SPIN=1` restores the spin).
- `gpu.xenos.execute_unclipped_draw_vs_on_cpu` defaults to `true` (Xenia's default; the SDK's is
  `false`). The practice court behind the main menu draws an unclipped rectangle with an 8192
  scissor into a 4x MSAA view of its depth buffer. Without running that vertex shader on the CPU
  the SDK sizes the target to all of EDRAM, it wraps over the color buffer, and the court went black
  for 5 to 10 seconds whenever no light flares were on screen. It costs about 1 fps in game
  (57.1 against 58.4) and lowers GPU use (57% against 70%).
- `gpu.vsync=false` (with `gpu.d3d12.allow_tearing=false`): the game paces itself on the SDK's
  60 Hz vblank, and waiting for the display's vsync as well made it miss frames whenever the two
  clocks drifted into phase (12 of 24 five-second windows at a clean 60 with vsync, 22 of 24
  without). Frames still do not tear: the swap chain shows the newest one on each refresh.
- `gpu.anisotropic_override=-1`: the game's own texture filtering, as on the console, instead of
  the SDK's forced 4x (Options, Texture filtering, raises it).
- `ui.aspect_ratio=16:9`: the game renders 16:9, and keeps that shape on 16:10 and ultrawide
  displays.
- `app.cache_root={app.root}/cache`: compiled shaders and pipelines are kept in `cache/` beside
  `user/` and loaded on later runs (`run_win.cmd` uses `out\cache`). The SDK's default is no
  cache folder, which keeps nothing: every run compiled each shader again as it first appeared,
  with hitches up to 160 ms in the first minutes of a game that a warm cache does not have.
- Render resolution 2x (1440p) held 60 fps in game on the A750 and 3x (2160p) about 40; the first
  start after a change spends a second or two rebuilding pipelines. FXAA (Options, Anti-aliasing)
  costs no measurable fps. Every run, at any setting, drops to a steady 30 fps for about 30 s of
  the pregame presentation with the CPU and GPU mostly idle; it looks like the game's own pacing
  of that sequence and returns to 60 when play starts.
- D3D12 on Windows. `--gpu.driver=vulkan` also runs there and draws the game the same, but holds
  only about 30 fps in game on the A750 (and logs pipeline cache errors), so Options does not
  offer it.

Measuring: `NCAABB10_PERF=1` logs fps, frame times, CPU and the busiest threads every 5 s; `NCAABB10_PROFILE=1` (Windows only) adds a sampling profile of guest functions every 10 s (it
costs fps while on), and `NCAABB10_PROFILE=callers` also names the game or SDK function behind
time spent in the C runtime, the OS or the GPU driver; `tools/perf_run.ps1 -Out <folder>` runs a
game and prints in-game fps and GPU use. `NCAABB10_AUDIO_DUMP=<file.wav>` records the game's
6-channel mix as it goes to the audio driver, and `python -I tools/audio_check.py <file.wav>`
summarizes it (levels per half second, silences, clipping and discontinuities).

Where the time goes in game (4 runs, A750): live play averages 58-60 fps and the busiest host
thread is the GPU command processor, at 0.4-0.6 of a core on average and close to a full core in
the heaviest stretches, where a 5 s window can dip to about 50 fps. Its time splits into the GPU
driver recording the frame's D3D12 commands (15-19%), copying guest memory the game rewrites every
frame into the GPU's copy (12-16%, about 700 MB/s), page protection for those watches (about 5%)
and register writes (about 9%). Tried and left off:

- Keeping the title's vblank at 60 Hz with `gpu.vsync=false` (an SDK experiment, not kept; the
  SDK ties both to `gpu.vsync` and raises it every 1 ms without vsync): the game waits
  for a vblank per frame, so frames a little over 16.7 ms turn into 33 ms ones (50-54 fps). The
  SDK's 1 ms vblank without vsync lets them slip by a millisecond instead.
- `gpu.xenos.clear_memory_page_state=false`: the SDK re-uploads every CPU-supplied page each frame
  by default (Xenia only does this for a few games); off cuts the copies from about 700 to 480 MB/s
  but did not change fps (58.4-59.5 against 58.9-59.1), and it would let writes the watches miss
  go stale, so it stays on.
- `gpu.xenos.readback_memexport=false`: the per-frame uploads are the game's own writes, not
  memexport readback; no change.

## Linux

Builds and runs on x86-64 Linux with the Vulkan driver (tested on Ubuntu 24.04 under WSL2). The
patched SDK and the `vulkan-deps` clones go beside this folder as on Windows:

```
tools/fetch_toolchain.sh    # or: sudo apt install clang-20 lld-20 libx11-xcb-dev
tools/setup_sdk.sh          # SDK v0.11.0 + patches, Vulkan 1.4.363 headers and loader, VMA; builds them
./build_linux.sh            # out/build/linux-amd64-patched/Release/ncaabb10
```

`tools/setup_sdk.sh` does what `tools\setup_sdk.cmd` does on Windows and takes the same
arguments. It builds the Vulkan pieces with `tools/build_vulkan_deps.sh` and the SDK with
`tools/build_sdk.sh`. `tools/check_tools.sh` says which tool is missing.

- `tools/fetch_toolchain.sh` needs no root: it downloads clang-20, lld-20 and libx11-xcb-dev (about
  31 MB) from the system's own apt archive with `apt-get download`, unpacks them under
  `~/.local/ncaabb10-toolchain` and writes an `env.sh` there that the build scripts source.
- Ubuntu 24.04's `libvulkan-dev` (1.3.275) is older than the 1.4.313 the SDK needs, hence
  `tools/build_vulkan_deps.sh`. The game itself loads the system's `libvulkan.so.1`.
- Also needed (already on a desktop Ubuntu): cmake, ninja, g++-13's libstdc++, and the Wayland,
  xkbcommon, libdecor, ALSA, PulseAudio and PipeWire development packages SDL3 builds against.
- Under WSL, build from copies on the Linux file system (for example `~/ncaabb10/`, with
  `REXGLUE_SDK_SRC` pointing `tools/build_sdk.sh` and `build_linux.sh` at the SDK copy if it is not
  beside the game's, and `assets` linked to the
  Windows folder); building under `/mnt/c` is much slower. The game build takes about 3.5 minutes
  with 16 jobs; it regenerates the recompiled code first, so it needs `assets/default.xex`.

Run it like the Windows build: `ncaabb10 --title.game_root=<assets>`. Checked under WSL2: boots
through the intros to the title screen and into the first-run profile screens with a controller
(the input feed works the same way), and renders the same as on Windows. WSL has only Mesa's CPU
Vulkan driver (llvmpipe), so it runs at about 30-40 fps there on 8 cores, and this WSL setup had no
sound server, so it ran silent (SDL3 is built with PipeWire, PulseAudio and ALSA). Not yet tried on
a Linux machine with a real GPU.

## Automated testing

`tools/drive.ps1` starts the game, feeds it keys through a file (the game follows the file named by
`NCAABB10_INPUT_FILE` and injects the keys in process, so nothing goes to the desktop's focused
window), and screenshots the game window with PrintWindow:

```powershell
& tools\drive.ps1 -Exe out\build\win-amd64\Debug\ncaabb10.exe -OutDir out\shots `
  -GameArgs "--title.game_root=`"$PWD\assets`" --ui.fullscreen=false --hid.mnk_mode=true" `
  -Steps "wait:36","key:RETURN","wait:10","shot:after_start"
```

`NCAABB10_TEST_ISO=<path>` makes the launcher start the disc image setup on that file, as if
chosen in the file dialog (which a script cannot drive); the log reports the outcome.
`NCAABB10_TEST_IMPORT=<folder>` does the same for **Bring over saves from an older copy**.
[HARDWARE_TEST.md](HARDWARE_TEST.md) lists the checks that need hardware the port has not run on
(physical controllers, other GPUs, 4K and display scaling, Linux on a real PC).
`python -I tools/make_test_isos.py <dir> assets/default.xex` writes small images for it: a good
one, the wrong version, not a disc, no `default.xex`, a looping and an oversized file list,
escaping and invalid names, a Windows device name, names that differ only in case, and short reads.

A virtual SDL controller stands in for a real one: `pad:<BUTTON>[:<ms>]` presses a button
(A B X Y BACK GUIDE START LS RS LB RB UP DOWN LEFT RIGHT), `padhold:<BUTTON>` and `padup:<BUTTON>`
hold and release one, and `axis:<LX|LY|RX|RY|LT|RT>:<value>` sets a stick or trigger
(-32768 to 32767). It attaches on the first such step; `drive.ps1` sets
`SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` so it works while the window is in the background.
`<BUTTON>@1` and `<AXIS>@1` (for example `pad:A@1`) drive a second controller, which the game
sees as player 2, and `unplug:<0|1>` pulls one out; its next step plugs it back in. The log
records the vibration the game sends to each (`test controller 0 rumble low .. high ..`).
`text:<text>` types into the focused text field (Options' player name).

Steps also include `ui:<KEY>[+ALT]`, which pushes the key through the window's own event queue so
it reaches the launcher and the F1/F11/Alt+Enter binds, `winsize:<name>`, and
`resize:<width>x<height>`, which sets the window's client size (Windows keeps it within the
screen, so a 1080p monitor cannot show the UI's 2x step this way). For example, the
launcher check is
`-Steps "wait:8","ui:F1","wait:2","ui:ESCAPE","wait:2","ui:RETURN","wait:30","shot:title"`.

`tools/to_game.ps1 -Out <folder>` drives a Release build from boot into a Duke at North Carolina
game and screenshots it (it needs a profile save with autosave on in the build's `user` folder).

Steps are `watch:<seconds>:<interval>:<prefix>`, `wait:<seconds>`, `key:<System.Windows.Forms.Keys name>` and `shot:<name>`. Waits are
fixed, so leave margin: loads take 30-60 s in a Debug build. Waits take fractions (`wait:0.4`);
a `ui:` press takes the game about 0.2 s to deliver, so leave at least that between presses.
