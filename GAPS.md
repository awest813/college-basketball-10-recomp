# Gap list

Audit of 2026-11-25, at commit 0aedae7; groups 1 and 4 updated the same day. The items run most important first within each group.
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
   - **Done:** the repo is under the MIT license (`LICENSE`, also in the zip as `LICENSE.txt`).
     It covers the repo's own files, not the game, the generated code or the SDK. The SDK patches
     are offered under the SDK's BSD 3-Clause license.
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
8. **The Vulkan backend on Windows is slow.**
   - **Checked:** `gpu.driver=vulkan` boots into a Play Now game and draws it the same as D3D12.
   - It holds only about 30 fps in game on the A750, against 60 on D3D12, and logs hundreds of
     `VulkanPipelineCache ... vertex_shader_not_found` errors.
   - Options does not offer it. Why it's slow is not investigated.
8a. **Renderer audit (Windows, D3D12, 2026-11-25).** Each setting was driven into a Play Now
    game with `NCAABB10_PERF=1`.
    - **Fixed: no shader cache in packaged builds.** The SDK's default `app.cache_root` is
      empty, which stores nothing. Only `run_win.cmd` passed a folder. The port now defaults to
      `cache/` beside `user/`.
      - A cold-cache run had a 161 ms hitch that the warm run did not.
      - Checked: the first run creates the cache, and the second loads from it.
    - **Added:** Options > Anti-aliasing (FXAA, the SDK's swap post effect). It costs no
      measurable fps, and the broadcast text stays sharp.
    - **Measured:**
      - 2x resolution holds 60 fps, with a one-time pipeline rebuild after the change.
      - 3x resolution runs at about 40 fps; the Options tooltip now says so.
      - ROV runs at 13-50 fps.
      - 16x filtering costs nothing measurable.
      - All of them draw correctly.
    - **Open:**
      - Every run holds a steady 30 fps for about 30 s of the pregame presentation, with the CPU
        and GPU idle. This looks like the game's own pacing, but it isn't confirmed against a
        console.
      - Exclusive fullscreen was not tried, since it changes the monitor's display mode.
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

Worked on 2026-11-25; items 13-16 are done.

13. **Debug logging on every start: done.** The key binding dump and the `FindBySetting` probes
    are gone from `OnTitleLaunch`. Binding parse errors still warn.
14. **Harmless errors in every log: done.** A drive into a game now logs no errors.
    - **Double frees:** `MmFreePhysicalMemory` skips a free of memory that is not allocated.
      With tracing on, there were exactly six such frees, all from `826C066C`, and all were
      skipped. The SDK would have refused them anyway.
    - **`no mount covers`:** logged at debug level now, through the quiet-file-probes patch.
    - **Delete on close:** also at debug level now.
    - **The misleading "opening it for read" warning:** gone.
    - **Still there:** five honest `NtCreateFile ... 0xc0000022` warnings for the `dirinfo` probes
      and the `XLiveBaseUnk58046` stub. The README lists them.
15. **Linux "Open game folder": done.**
    - It now uses `SDL_OpenURL` with a percent-encoded `file://` URL, with no shell and any
      folder name.
    - Builds clean. Not run on a Linux desktop.
16. **Settings only in config files: mostly done.**
    - Options has **Keyboard controls**: each of the 25 pad inputs with its keys, a **Change**
      button that takes the next key, and **Reset all keys**.
    - **Checked:**
      - Rebinding A to G is saved, and after a restart G presses A.
      - Reset removes the changed keys from the options file, and Space is A again.
    - The renderer stays in config files, on purpose (see item 8).
17. **Test hooks ship in the Release exe.**
    - These are the input-file feed, the virtual controllers, `NCAABB10_TEST_ISO`,
      `NCAABB10_TEST_IMPORT` and `NCAABB10_TEST_CRASH`.
    - They're inactive unless an environment variable is set, so this is acceptable, but noted.

## 5. Project upkeep

18. **The work exists only on this disk: mostly done.**
    - The project is on GitHub.
    - The SDK branch `ncaabb10-fixes` still has no remote. Its seven patches are in `patches/`,
      and `tools/setup_sdk.cmd` / `.sh` rebuild the branch's source from them on a clean
      v0.11.0.
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
21. **The patched SDK built Release only: done.** `tools/build_sdk.cmd 0 debug` (or
    `relwithdebinfo`, `all`) builds it in that configuration too, installed beside Release. The
    patched presets have Debug and RelWithDebInfo builds, so a Debug session runs the same SDK
    fixes as Release.
22. **Build and debug audit (2026-11-25).**
    - **Fixed:**
      - Setting up the patched SDK took seven `git apply` lines, three Vulkan clones and two
        build scripts. `tools/setup_sdk.cmd` / `.sh` does all of it and skips what is done.
        Checked from an empty folder on Windows; on WSL against an already patched copy.
      - A missing tool, `assets/default.xex`, SDK or SDK configuration gave CMake or compiler
        errors. The build scripts now stop first with what to do. `tools/check_tools` finds
        LLVM's default Windows folder and checks for clang 20.
      - The job count was fixed at 12-14; it is now the processor count.
      - The log never said which build wrote it. Its first `ncaabb10:` line, and the launcher,
        now give the version, commit and configuration.
      - Logs piled up forever (320 files in one test folder). The port keeps 50 MB of old ones.
      - Crash reports gave only `module+offset`, and a Release build had no PDB.
        - Release now links `ncaabb10.pdb` (12 MB, about 7 s), and the SDK's Release PDBs are
          installed.
        - `tools/symbolize.py` names each frame, down to `sub_<guest address>` for recompiled
          code.
        - `tools/package.ps1` writes a symbols zip with each package.
        - Checked with `NCAABB10_TEST_CRASH`.
      - A Debug build printed 2675 harmless SIMDe vectorization warnings; now 0.
      - Visual Studio's vcvars printed a `vswhere.exe` error on every build; hidden.
    - **Open:** no CI (it could build the SDK, but not the game, which needs `default.xex`).
      `symbolize.py` is untried on a Linux crash.
