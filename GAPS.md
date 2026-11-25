# Gap list

Audit of 2026-11-25, at commit 0aedae7; group 1 updated the same day. The items run most important first within each group.
"Checked" means it was run and looked at, not only read in code.

What already holds:
- A full Play Now game plays to the post-game menu at about 58 fps.
- Saves, profile and Dynasty load work.
- Both platforms build with 0 warnings.
- The seven patch files apply to a clean v0.11.0 and produce exactly the `ncaabb10-fixes`
  branch.
- The working tree is clean.

## 1. Before sharing a build with anyone

Worked on 2026-11-25. Each item says what is done and what is still open.

1. **Licenses and notices.**
   - **Done:** the zip carries `THIRD_PARTY_NOTICES.txt`, built by `tools/notices.py` from the
     SDK source the build used. It covers:
     - the SDK and its Xenia portions
     - SDL3, ImGui, spdlog, toml++, xxHash, SIMDe, stb, excrypt, TLSF, the AMD DXBC checksum and
       the RenderDoc header
     - glslang, VMA and Vulkan-Headers
     - FFmpeg and libmspack, the two LGPL libraries
   - The file also says which DLL holds each library, which FFmpeg decoders are built, and how to
     rebuild the two DLLs that hold the LGPL code.
   - **Open:** the repo has no LICENSE of its own. Choosing one is the owner's call.
   - **Open, the owner's call:** `ncaabb10.exe` contains code translated from the game's
     `default.xex`. The repo holds none, but the zip does. Other static recompilation projects ship
     this way too; it's a legal question to weigh before sharing builds publicly.
2. **Only one GPU has been tested.**
   - **Lower risk than first listed:** on NVIDIA and AMD the SDK already picks the `rtv` render
     path itself (`render_target_cache.cpp`: ROV only on Intel), so the port's default only
     changes Intel. The other defaults don't depend on the vendor.
   - **Open:** never run on NVIDIA or AMD. [HARDWARE_TEST.md](HARDWARE_TEST.md) has the steps.
3. **No physical controller has been tested.**
   - **Open:** only SDL's virtual controller was used. HARDWARE_TEST.md has a five-step pad check
     (launcher, replug, in game, Guide and Game Bar, two pads).
4. **Updating used to lose the player's saves.**
   - **Done:**
     - The README documents updating. The zip holds no `user/`, `game/` or options file, so
       unpacking over the old folder keeps everything.
     - A copy without saves now offers **Bring over saves from an older copy**. It copies the old
       copy's saves and options, and plays from its `game/` when that is set up.
     - A chosen game folder is now saved at once, not only on Play.
   - **Checked:** in a fresh copy, two saves and the options came over. After a restart, Play
     loaded the imported profile (the game's autosave notice). An empty folder gives a clear
     error.
5. **The exe had no icon, version information or signature.**
   - **Done:**
     - An original basketball icon, drawn by `tools/make_icon.py`, sits in the exe resource.
       Explorer and the window use it.
     - Linux sets the same icon on the window through SDL (checked: a 64x64 `_NET_WM_ICON`).
     - Version 0.1.0 is in the exe's Details and on the launcher.
     - The README explains SmartScreen's "Run anyway".
   - **Open:** code signing needs a certificate, which costs money and needs an identity. It's
     the owner's call.

## 2. Platforms not yet covered

6. **Linux has never run on real hardware.**
   - It was tested only under WSL2: llvmpipe at 30-40 fps, with no sound server.
   - Never tried there:
     - a real GPU or a desktop session
     - audio
     - a window manager's fullscreen
     - the file picker (the ISO setup was tested through `NCAABB10_TEST_ISO`)
     - holding Shift at startup, which WSL can't send
7. **Linux has no package.**
   - There's no tarball script.
   - There's no stated set of runtime libraries (libstdc++ from g++-13, `libvulkan.so.1`, SDL3's
     Wayland/X11 and audio backends).
   - The `linux-amd64` (stock SDK) preset was never built.
8. **The Vulkan backend on Windows ships untried.** `rexgpu-xenos-vulkan.dll` is in the zip and
   `gpu.driver=vulkan` selects it, but it was never run on Windows.
9. **Real high-resolution and high-DPI displays were not checked.**
   - The 1440p and 4K layouts were checked only on Xvfb virtual screens.
   - Windows was checked at 720p and in an 800x600 window.
   - A real 4K monitor and Windows display scaling at 125-200% were not tried.

## 3. Game coverage

10. **Most modes were never played through.** Checked:
    - Play Now: a full game.
    - Dynasty: save, load to the same week, and quit.
    - Profile, roster saves, the online menus' offline message, and player 2 in a full game.

    Not checked:
    - simulating or playing a Dynasty season through to the postseason
    - tournament modes
    - create-a-player (`createplayercache1.big`)
    - practice and the tutorials
    - the attract and tutorial movies (`data/gfx/movies/*.vp6`); the boot logo movies play
11. **The longest run was 51 minutes.** No multi-hour session or season simulation was run, and
    memory growth over time was never tracked.
12. **Audio was checked only by measurement.**
    - The record shows levels and dropouts from `NCAABB10_AUDIO_DUMP` and
      `tools/audio_check.py`, and no listening check.
    - The 6-channel to stereo mix, commentary sync and the fight songs weren't confirmed by ear.

## 4. Polish and code

13. **Debug logging is left in on every start.** `OnTitleLaunch` (`src/main.cpp:381-394`) logs
    every key binding's sources and two `FindBySetting` probes.
14. **Alarming but harmless errors appear in every log.** Each load logs these at error level:
    - six `BaseHeap::Release failed` / `PhysicalHeap::Release failed` pairs
    - `no mount covers` for `UPDATE:\`, `rmcsave:` and `ShaderDumpxe:`
    - an `unimplemented: XLiveBaseUnk58046` warning

    A player reading the log takes them for faults. Options: mount stubs for `UPDATE:` and
    `rmcsave:`, or lower the double-free message for this known caller (`826C066C`).
15. **Linux "Open game folder" can silently do nothing.**
    - It uses `system("xdg-open '...'")` and skips any path containing `'`.
    - Starting `xdg-open` directly (fork/exec), or with `SDL_OpenURL("file://...")`, would avoid
      the shell entirely.
16. **Some settings exist only in config files.**
    - Buttons are remapped through `hid.keybind_*` in `ncaabb10.toml`, or the F4 SDK panel.
    - The renderer (D3D12 or Vulkan) is chosen the same way.
17. **Test hooks ship in the Release exe.**
    - These are the input-file feed, the virtual controllers and `NCAABB10_TEST_ISO`.
    - They're inactive unless an environment variable is set, so this is acceptable, but noted.

## 5. Project upkeep

18. **The work exists only on this disk.**
    - Neither the project (33 commits) nor the SDK branch `ncaabb10-fixes` has a remote.
    - One disk failure loses both.
19. **There's no regression suite or CI.**
    - `drive.ps1`, `to_game.ps1`, `perf_run.ps1` and `make_test_isos.py` are run and judged by
      hand.
    - Nothing asserts results. For example, there's no check that every bad test ISO is rejected,
      or that a scripted boot reaches the main menu within N seconds.
20. **The fixes are not upstream.**
    - Six of the seven SDK fixes exist only as patches here (the texture-watch race was fixed
      upstream independently). The dcbz bug is upstream issue #627, still open.
    - The nightly's boot stall from its critical-section handoff (`1a8d4199`, `ccc231e9`) is
      unreported.
    - Until these land, the port is pinned to SDK 0.11.0.
21. **The patched SDK builds Release only.** Debug builds have to use the stock SDK, which still
    has the missing-text, dirty-disc-freeze and dcbz bugs, so a Debug session can't reproduce
    Release behavior.
