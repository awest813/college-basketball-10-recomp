# Hardware check

These are the checks that need hardware the port hasn't run on yet. Each one takes a few minutes.

**Before you start**
- Use a Release build or the packaged zip.
- The game must be set up (the launcher says "Ready to play").
- Every run writes a log to `logs/` beside the executable. Note what you saw, and send the newest
  log with it.

## A physical controller

Try an Xbox pad, a PlayStation pad, a Switch Pro pad, or any pad you have.

1. **Launcher and Options**
   - Start with the pad plugged in.
   - The launcher should name the pad under the buttons.
   - The D-pad and left stick move between buttons, and A presses one.
   - The Guide button (or Back + Start) opens Options, and B closes it.
2. **Plugging in and out**
   - Unplug the pad while the launcher is open, then plug it back in.
   - The pad's name should go away and come back, and the pad should still work.
3. **In game**
   - Press Play and play a few minutes of a Play Now game.
   - Check that the sticks, triggers and every button act as on a console.
   - Check that vibration works: a dunk or a foul rumbles.
4. **Game Bar**
   - On Windows, press the Xbox button in game.
   - Options should open. Game Bar may open as well unless it's turned off in Windows settings;
     either is expected. Note which happened.
5. **Two pads**
   - Plug in a second pad and start a game with two players.
   - The second pad should control player 2.

## Another GPU (NVIDIA, AMD, or another Intel)

1. Before starting, set the environment variable `NCAABB10_PERF=1`. This logs fps every 5 seconds.
2. Watch the boot movies, the title screen, and the main menu with the practice court behind it.
   - Nothing should flash black or show garbage.
3. Play a full half of a Play Now game.
   - Check the score bar, the lineups and the replays.
4. Send the log. Its `perf` lines give the fps. On the A750, live play holds about 58-60.
5. If something draws wrong, run again with `--gpu.d3d12.render_target_path=rov` and say whether
   that fixes it.
   - On NVIDIA and AMD this port uses the same render path the SDK already picks there by default.
     Only on Intel does it pick a different one from the SDK.

## A 1440p or 4K monitor, and Windows display scaling

1. **Fullscreen** (F11) on a 1440p or 4K monitor:
   - The launcher and Options should grow in whole steps: 2x at 1440p, 3x at 4K.
   - Text should stay sharp.
2. **Display scaling:** set Windows display scaling to 125%, 150% and 200% (Settings > Display),
   then start the game windowed. For each:
   - the window should fit the screen
   - the launcher should be readable
   - F11 should go fullscreen and back

## Linux on a real PC (not WSL)

1. Build with `build_linux.sh`, or use a Linux build you were given.
2. Start the game with no command line options. Then check:
   - **Window and launcher:** the window has the basketball icon, and the launcher appears.
   - **Disc setup:** "Set up from disc image..." opens a file picker. This needs
     xdg-desktop-portal or zenity.
   - **Game folder:** "Open game folder" opens your file manager once the game is set up.
3. Hold Shift while starting, with "Show this launcher at startup" unticked. The launcher should
   still appear.
4. Play a few minutes with `NCAABB10_PERF=1`. Check that sound plays, and send the log.
