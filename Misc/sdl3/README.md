# SDL3 migration: provenance and results

Records for the SDL3 migration plan of 2026-09-12, which is kept outside this branch.
This file covers Task 1, the SDL2-compatible preparation, Task 2, the SDL3 foundation, Task 3,
video transitions and gamma, Task 4, input, Task 5, audio streams, Task 6, builds, packaging and CI,
and Task 7, integrated validation.

The current branch supports SDL3 only. Earlier sections retain the intermediate SDL2 preparation results;
they do not describe supported builds. See the [follow-up review](#follow-up-review-2026-09-14) for the latest
fixes and verification.

## Baseline

| Item | Value |
|---|---|
| Revision | `25108a2fe` (qsrebase tip, 2026-09-13) |
| SDL (macOS) | vendored `macOS/SDL2.framework` 2.32.10 (loaded as `@rpath/SDL2.framework`, current version 3201.10.0) |
| SDL (Windows) | vendored `Windows/SDL2` 2.32.10 |
| SDL (Linux CI) | `libsdl2-dev` on `ubuntu-latest`, which GitHub maps to Ubuntu 24.04 (SDL 2.30.0; checked 2026-09-13) |
| Host | MacBookPro18,3 (Apple M1 Pro), macOS 27, built-in display (1024-entry gamma table) |
| Video | SDL `cocoa`, OpenGL 2.1 Metal (`GL_RENDERER: Apple M1 Pro`), 120 Hz, 255 ppi |
| Display modes | 22 from `vid_describemodes`, 3024x1964 down to 960x600, all 32-bit at 120 Hz |
| Audio | SDL `coreaudio`, 44100 Hz, 16-bit stereo mix, 1024-sample device request, 65536-byte DMA buffer |
| Gamma policy | GLSL gamma through the scene FBO by default; hardware ramps with `-hwgamma`, obtained MSAA, or no usable FBO |
| Build configurations | Xcode Debug `-O0` and Release `-O2`, x86_64 + arm64, macOS 12.0 deployment target |
| Baseline Debug executable | SHA-256 `738e42cb386b0ceb…` |
| Baseline Release executable | SHA-256 `478058affa689d7e…` (x86_64 + arm64) |
| Branch Release executable, measured | SHA-256 `2e536b0bfeee95a7…`, built before the final review fixes |
| Branch Release executable, final | SHA-256 `459c40a654a7f32f…`, built from this change's engine sources |

Executable hashes cover ad-hoc local builds, so they only identify the binaries measured here.

## Changes

| Area | Change |
|---|---|
| Entry point | `QSSM_Main` extracted; both Cocoa launcher routes call it and other platforms use a thin `main()` |
| SDL 1.2 | Source branches, the `USE_SDL2` switch, `cd_sdl.c` (every build uses `cd_null.c`), the dead `SDLApplication` class and a stale `SDL_WM_ToggleFullScreen` message removed |
| SDL2 version guards | `SDL_VERSION_ATLEAST` guards satisfied by 2.30 removed |
| Build tooling | Watcom, CodeBlocks and legacy `.vcproj` retired; `-sdl2` cross-build wrappers folded into the unsuffixed scripts; `USE_SDL2` dropped from scripts, CI, harnesses and build docs |
| Mouse | Float motion through accumulation, with scaled MenuQC/CSQC cursor positions still truncated to whole pixels |
| Ticks | 64-bit tick storage except Discord's wrap-safe 32-bit cache; fixes modvote after ~24.8 days of uptime |
| Diagnostics | `SNDDMA_GetDeviceName`; `VID_GetCurrentDPI` declared in `vid.h` |
| Gamma | macOS hardware gamma through CoreGraphics with the display's original table saved and restored |
| Docs | `Quakespasm.txt`/`.html` no longer describe SDL 1.2 builds or `cd_sdl.c` |
| Tests and records | `test_mouse_motion.py`, `test_tick_deadlines.py`, and the `Misc/sdl3` benchmark and gamma tools |

## Verification

- **Source removal equivalence.** The SDL 1.2 branches and version guards were removed with `unifdef`.
  Every touched file preprocessed identically before and after for Apple, Windows and Linux
  configurations (includes stripped, `USE_SDL2` defined on the original side).
- **Builds.** macOS Debug after each step with no new warnings in touched files; macOS Release
  universal; MinGW cross builds `build_cross_win64.sh` and `build_cross_win32.sh` link
  `quakespasm.exe` (GUI subsystem, `SDL2main`, imports `SDL2.dll`) with no warnings in touched files;
  Win64 was rebuilt after the review fixes with the same result.
  A `-Wshorten-64-to-32` audit build found no remaining tick truncation. The pre-7.66 curl
  `curl_multi_wait` branch in `net_main.c`, which neither vendored curl (macOS vcpkg, Windows 8.21.0)
  compiles, passes the Xcode compile command with `-fsyntax-only -Werror=shorten-64-to-32` apart
  from the pre-existing `slistLastShown` warning.
- **Harnesses**, all passing:

  ```sh
  python3 Misc/stress/test_mouse_motion.py
  python3 Misc/stress/test_tick_deadlines.py
  python3 Misc/stress/test_menu_text_input.py
  python3 Misc/stress/test_utf8_to_quake.py
  python3 Misc/stress/test_console_completion.py
  ```

  `test_mouse_motion.py` failed on the integer path before the float change, and its scaled-cursor
  cases caught a regression in the first float version before truncation was restored.
  `test_tick_deadlines.py` drives the real
  modvote, GitHub-wait and Discord community cache timing across UINT32_MAX, and includes the old
  signed 32-bit `SDL_TICKS_PASSED` expression as a contrast case that misjudges a 25-day-old vote.
- **Other harnesses.** `test_alias_visual_probe`, `test_bsp_miptex`, `test_chunked_download`,
  `test_demo_list`, `test_harness_hardening`, `test_hotpath_lookups`, `test_map_categories`,
  `test_quit_http` and `test_teleport_scissor` pass. `test_bsp_lump_bounds`, `test_msgread_bounds`
  and `test_suggestion_cap` need a `QSSM_STRESS` engine build, and `test_chunked_download_udp` and
  `test_teleport_scenecache` need `--pak`/`--basedir` inputs; those five were not run, and the first
  three stop identically at the baseline revision.
  `test_quit_udp.py` (GNU ld `--gc-sections`) and `test_teleport_gl.py` (`-lGL`) are Linux-only and
  fail to link on macOS, identically at the baseline revision.
- **Launcher route.** `AppController` is the application delegate and `launchQuake:` is the only live
  path to `QSSM_Main` for command-line, Finder and Start-button launches; `SDLMain` is only the
  xib's File's Owner, so its `applicationDidFinishLaunching` route is unreachable. Command-line runs
  therefore exercise the real launch path, but not the Start button itself.
- **Update helper.** `-qssm-update-helper` without a manifest returns 2 through `QSSM_Main` on both
  builds, so helper dispatch survives the entry-point change.
- **Release packaging.** `macOS/verify-macos-release.sh` passes for the baseline and final branch
  Release builds on a staged archive with the expected top-level files: bundle identifier and
  version, x86_64 + arm64 slices, macOS 12.0 minimums, SDL2 embedded through `@rpath`, the Dock
  plugin, a strict code signature and no `get-task-allow`. `build-macos.sh` itself was not run
  because it re-runs `setup-vcpkg.sh` against the shared dependency tree.
- **Runtime.** Update and data-import self-tests pass through the new entry point; dedicated
  init and quit is clean; a windowed client initializes CoreAudio and logs
  `coreaudio - MacBook Pro Speakers` from `SNDDMA_GetDeviceName`.
- **Hardware gamma.** `python3 Misc/sdl3/gamma_lifecycle.py <label> <QSS-M.app>` on the baseline and
  the branch (Debug), 5/5 steps each. Full 1024-entry tables were identical between the two builds
  after launch, after refocusing and after `vid_restart`. With another app focused the branch
  restored the system table exactly (3072/3072 values); the SDL2 build was off by up to 1.6e-5
  because it restored a resampled 256-entry ramp. The default GLSL path left the table untouched.

Hardware-gamma paths, baseline versus branch Debug builds, `gamma 0.6`:

| Path | Command | Baseline | Branch |
|---|---|---|---|
| Windowed `-hwgamma` | `gamma_lifecycle.py <label> <app>` | 5/5 | 5/5 |
| MSAA 4x, default selection | `--no-hwgamma --cfg 'vid_fsaa "4"' --cfg 'vid_fullscreen "0"'` | 5/5 | 5/5 |
| Desktop fullscreen | `--cfg 'vid_fullscreen "1"' --cfg 'vid_desktopfullscreen "1"' --settle 2` | 5/5 | 5/5 |
| Exclusive fullscreen | `--cfg 'vid_fullscreen "1"' --cfg 'vid_desktopfullscreen "0"' --settle 2` | 4/5 | 4/5 |

MSAA 4x was obtained and selected hardware gamma without `-hwgamma`. In every path the applied
ramps were byte-identical between the builds, and in the windowed, MSAA and desktop-fullscreen
paths the branch's focus-loss restore matched the system table exactly where SDL2's was off by
up to 1.6e-5. In exclusive fullscreen `open -a Finder` did not take focus from either build, so
both kept the ramp at that step with identical tables; the branch restores on every
`SDL_WINDOWEVENT_FOCUS_LOST`, so no focus-loss event arrived. Focus-loss restore in exclusive
fullscreen is therefore unchanged but not exercised. Only one display was attached.

## Measurements

Release builds, runs interleaved between the two apps, medians of five. Scratch basedir holding
only id1 `pak0.pak`/`pak1.pak`, launched from the command line (no Cocoa launcher), warm cache:

```sh
python3 Misc/sdl3/bench.py results.json baseline=<baseline QSS-M.app> branch=<branch QSS-M.app>
```

| Measurement | Baseline `25108a2fe` | Branch |
|---|---|---|
| timedemo demo1, 1280x800 window (fps) | 936.3 | 958.1 |
| Launch to "Quake Initialized" (s) | 0.509 | 0.486 |
| Quit to exit (s) | 0.286 | 0.292 |
| Launch to ready, `-nosound` (s) | 0.360 | 0.339 |
| Quit to exit, `-nosound` (s) | 0.208 | 0.210 |
| CoreAudio init, sound minus `-nosound` (s) | 0.149 | 0.147 |
| CoreAudio shutdown, sound minus `-nosound` (s) | 0.079 | 0.083 |

All differences are within run-to-run spread. Timedemo runs ranged 901–962 fps for the baseline
and 933–1023 fps for the branch (demo1 lasts about a second at this speed). Launch-to-ready runs
stayed within about 55 ms of their medians, and quit runs within about 40 ms except two baseline
`-nosound` quits of 309 and 393 ms against a 207 ms median. These launch times are warm and exclude
the Cocoa launcher and a full mod install, so they are not comparable to a normal install's startup.

## Not covered

- Physical keyboard/mouse play, the launcher Start button, Dock menu and badges.
- Linux and Haiku compiles (no toolchain on the test Mac; the SDL 1.2 removal is covered by the
  preprocessor proof), MSVC builds, and running the Windows executables.
- Cold launches, multiple displays, and focus-loss gamma restore in exclusive fullscreen.
- A real self-update through the helper.
- The optional sdl2-compat diagnostic.

## Task 2: SDL3 foundation

The engine, launcher, harnesses and build files now target SDL 3 only. Two temporary gaps are
marked `TEMP(sdl3)`: `snd_null.c` stands in for `snd_sdl.c` in every build until the Task 5
audio stream port, and voice capture is disabled until then. Outside macOS, hardware gamma is
removed because SDL 3 has no window gamma API, so those platforms use GLSL gamma.

### Vendored SDL

| Item | Value |
|---|---|
| Release | SDL 3.4.16, tag `release-3.4.16` of `libsdl-org/SDL` on GitHub |
| `SDL3-3.4.16.dmg` | SHA-256 `675660a9e457239af615f9e41f788612168d1639b9d2eda2957e8dace26687fd` |
| `SDL3-devel-3.4.16-VC.zip` | SHA-256 `1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de` |
| `SDL3-devel-3.4.16-mingw.zip` | SHA-256 `9828bb735cf8a007bcf0ac5aa9f01f3fcb54b7ca67c932e775c905c5d5053a60` |
| `macOS/SDL3.framework` | The DMG's `SDL3.xcframework/macos-arm64_x86_64` framework, identical under `diff -r` (binary `546ce8509e3ead9e…`; x86_64 minimum 10.13, arm64 11.0; ad-hoc signed as shipped) |
| `Windows/SDL3/include` | VC package headers, less the 11 `SDL_test*` headers |
| `Windows/SDL3/lib/x86`, `lib/x64` | VC `SDL3.dll` and `SDL3.lib` plus MinGW `libSDL3.dll.a`, each byte-identical to its package; the VC and MinGW DLLs export the same 1271 symbols |
| `Windows/SDL3/LICENSE.txt` | Identical in both Windows packages |
| System SDL | The Linux `Makefile` requires `pkg-config` `sdl3 >= 3.2.12` and `< 4`; `quakedef.h` enforces the same floor |

### Changes

| Area | Change |
|---|---|
| Names | SDL2 identifiers renamed from SDL 3's `SDL_oldnames.h` table in one word-boundary pass; the compatibility names are not enabled |
| Results | Every call to the 146 functions whose 0-on-success `int` became a `bool` was checked, and tested results were inverted |
| Entry point | `SDL_main.h` in `main_sdl.c` supplies `WinMain` on Windows without an `SDL3main` library; the Cocoa launcher keeps `main()` through `SDL_MAIN_HANDLED` and `SDL_SetMainReady`; `NSPrincipalClass` is `SDL3Application` |
| Video | Displays addressed by `SDL_DisplayID`; fullscreen modes from `SDL_GetFullscreenDisplayModes`, deduplicated across pixel densities; desktop fullscreen through a NULL fullscreen mode and exclusive fullscreen through the exact or closest mode, each followed by `SDL_SyncWindow`; drawable size from `SDL_GetWindowSizeInPixels`; float refresh rates rounded |
| Display diagnostics | `VID_GetCurrentDPI` became `VID_GetCurrentDisplayScale` (`SDL_GetWindowDisplayScale`), so the video mode line and the video settings `say` report print a scale instead of the panel's physical ppi; the window does not request high pixel density, so a Retina panel reports `1x` |
| Gamma | macOS keeps the Task 1 CoreGraphics path; other platforms lose hardware ramps with `SDL_SetWindowBrightness` and keep GLSL gamma |
| Input | Window-scoped relative mouse mode and text input; float mouse positions; SDL 3 event fields (`key.down`, `key.scancode`, `gdevice`, `gtouchpad`, `gsensor`, wheel `integer_y`, `drop.data`); text input sized from the event string, which is now a pointer |
| Controllers | Joysticks opened by instance ID from `SDL_GetJoysticks`, while `joy_device` stays an index; LED and rumble support read from gamepad properties; `SDL_GetGamepadPowerInfo` mapped onto the existing empty, low, medium, full and wired levels |
| HID mouse startup | Explicit pending, ready, failed and abandoned states, so a timed-out start joins the thread before its mutex and condition are destroyed |
| Windows | HWND from window properties; the message hook returns `bool`; the low-level keyboard hook pushes `key.down`/`key.scancode` events |
| Linux | `SDL_HINT_VIDEO_DRIVER` defaults to `x11,wayland` and an explicit `SDL_VIDEO_DRIVER` still wins; the window icon colour key uses `SDL_MapSurfaceRGB` |
| Builds | Xcode links and embeds `SDL3.framework`; the MinGW Makefiles use `Windows/SDL3` without `sdl2-config`; the Visual Studio project, `build-w32.sh`, `build-w64.sh` and the MSYS2 and MSVC workflows ship `SDL3.dll`; `verify-macos-release.sh` checks the embedded `SDL3.framework` |
| Harnesses | SDL 3 names, `bool` results and pointer text events; `test_hid_start_wait.py` covers the HID startup states |

### Verification

- **Builds.** macOS Debug (arm64) and a clean universal Release build succeed, and the Release
  build adds no warning kinds over the SDL2 Release baseline. `build_cross_win64.sh` and
  `build_cross_win32.sh` link GUI-subsystem executables importing `SDL3.dll`, with no warnings in
  port files; all 140 SDL imports appear in the vendored DLL's exports. macOS Debug and both
  Windows targets were rebuilt after the final renames.
- **Linkage.** The Release executable and embedded framework are x86_64 + arm64. The executable
  links `@rpath/SDL3.framework/Versions/A/SDL3` (current version 401.16.0) through
  `@executable_path/../Frameworks`, and the framework exports all 139 SDL imports on each slice.
- **Release packaging.** `verify-macos-release.sh` passes for the Release app and a staged
  archive with the expected top-level files.
- **Runtime (arm64).** Dedicated init and quit print the same output as the SDL2 baseline. A
  1280x800 windowed client initializes video (120 Hz, GLSL gamma, OpenGL 2.1 Metal on the Apple
  M1 Pro), completes `timedemo demo1` and exits 0 on SIGTERM; sound reports the Task 5 stub. The
  x86_64 slice was not run because Rosetta is not installed on the test Mac.
- **Harnesses.** `test_alias_visual_probe`, `test_bsp_miptex`, `test_chunked_download`,
  `test_console_completion`, `test_demo_list`, `test_harness_hardening`, `test_hid_start_wait`,
  `test_hotpath_lookups`, `test_map_categories`, `test_menu_text_input`, `test_mouse_motion`,
  `test_quit_http`, `test_teleport_scissor`, `test_tick_deadlines` and `test_utf8_to_quake` pass
  against Homebrew SDL 3.4.12. `test_quit_udp` and `test_teleport_gl` now compile against SDL 3 and
  stop at their Linux-only link steps, as at the baseline. The stress-build and input-file
  harnesses were not run.
- **Linux-only code.** No Linux toolchain is available on the test Mac. Preprocessing every engine
  source for Linux, Apple and Windows found one SDL line compiled only on Linux, the video driver
  hint, and `pl_linux.c` and `sys_sdl_unix.c` pass `clang -fsyntax-only` against the SDL 3 headers.

### Not covered

- Sound output and voice capture (Task 5).
- Video mode transitions, multiple displays and gamma parity (Task 3). The `VID_SetMode` comment
  saying `SDL_ShowWindow` cannot report failure describes SDL2 and is rechecked there.
- Controllers, keyboard layouts, the macOS warp workaround and physical play (Task 4).
- CI and packaging left for Task 6: `linux.yml` and `valgrind.yml` still install `libsdl2-dev` and
  fail until SDL 3 is built there; the `windows.yml` package list, dependency watch, Visual Studio
  project name, removal of `SDL2.framework` and `Windows/SDL2`, and user documentation.
- MSVC builds, running the Windows executables, the x86_64 macOS slice, and benchmarks. The Debug
  timedemo runs were smoke tests on a loaded machine, not measurements.

## Task 3: video transitions and gamma

### Asynchronous fullscreen

SDL 3 applies window state changes asynchronously, while SDL2's Cocoa backend finished a fullscreen
transition before returning. An SDL 3.4.16 probe against the vendored framework showed:

- Leaving desktop (Spaces) fullscreen returns immediately while the window flags and size still report
  fullscreen. They settle only after `SDL_SyncWindow`. A size set before the sync is queued and applied.
- A hidden window accepts a fullscreen request, returns success and reports itself fullscreen, but the
  transition only happens when the window is shown.

`VID_SetMode` trusted the flags right after leaving fullscreen, so every desktop fullscreen to windowed
`vid_restart` logged "SDL reported success leaving fullscreen, but the window is still fullscreen;
keeping the current video mode", and the window then settled at its previous windowed size instead of
the requested one. The engine now calls `SDL_SyncWindow` after leaving fullscreen, before judging a
failed fullscreen request, in the windowed fallback (which first cancels any fullscreen request SDL still
holds), and before reading the drawable size. The hidden-window comment in `VID_SetMode` now describes
the verified SDL 3 behaviour.

### Mode transitions

```sh
python3 Misc/sdl3/vid_transitions.py <label> <QSS-M.app>
```

The script drives `vid_restart` over localhost rcon and compares `vid_describecurrentmode` immediately
after each restart with its report once the window has settled. Debug builds, arm64, built-in display:

| Step | SDL2 baseline | SDL3 before the sync fix | SDL3 |
|---|---|---|---|
| Mode list | 22 modes, 3024x1964 to 960x600 | identical | identical |
| Desktop fullscreen | OK | OK | OK |
| Windowed 800x600 | OK | FAIL: stayed fullscreen, settled 640x480 | OK |
| Exclusive 1800x1125 | OK | OK | OK |
| Windowed 640x480 | OK | OK | OK |
| Desktop fullscreen again | OK | OK | OK |
| Windowed 1024x640 | OK | FAIL: stayed fullscreen, settled 640x480 | OK |

Leaving exclusive fullscreen already settled before `SDL_SetWindowFullscreen` returned, so only the
desktop fullscreen exits failed. MinGW Win64 and Win32 builds compile the change without warnings.

### Hints

The engine sets no macOS fullscreen hints. `SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES` defaults to "1" in both
SDL 2.32.10 and SDL 3.4.16. SDL 3 documents `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS` as `auto` (exclusive
fullscreen windows minimize on focus loss), while the SDL 2.32.10 header says it defaults to false.

### Gamma lifecycle

`gamma_lifecycle.py` with the four Task 1 configurations, SDL2 baseline `25108a2fe` against SDL3 with
the sync fix, alternating per path (Debug builds, `gamma 0.6`):

| Path | SDL2 baseline | SDL3 |
|---|---|---|
| Windowed `-hwgamma` | 5/5 | 5/5 |
| MSAA 4x, default selection | 5/5 | 5/5 |
| Desktop fullscreen | 5/5 | 5/5 |
| Exclusive fullscreen | 4/5 | 5/5 |

Applied ramps, refocus, `vid_restart` and quit tables were byte-identical between the builds on every
path. With another app focused, SDL3 restored the system table exactly (3072/3072 values) on all four
paths, where the SDL2 baseline was off by up to 1.6e-5. In exclusive fullscreen `open -a Finder` did not
take focus from the SDL2 build, which kept its ramp, but did take it from the SDL3 build, which restored
the system table, so focus-loss restore in exclusive fullscreen is now exercised. This matches SDL 3's
documented `auto` minimize-on-focus-loss default; whether the window minimized was not checked.

## Task 4: input

### Gamepad face buttons

SDL 3 names face buttons by position. SDL 2.32 named Nintendo pads' buttons by their printed letters
unless `SDL_GAMECONTROLLER_USE_BUTTON_LABELS` was false, and QSS-M's key names follow those letters
(`K_ABUTTON` is "A" in the Nintendo column of `keys.h`). The positional SDL 3 mapping therefore made a
Switch pad's A button act as B. `IN_FaceButtonKey` now maps Nintendo face buttons through
`SDL_GetGamepadButtonLabel`, uses the Switch layout (east A, south B, north X, west Y) for buttons without
a letter label, and keeps pads positional when the environment sets `SDL_GAMECONTROLLER_USE_BUTTON_LABELS`
false, parsed like an SDL2 boolean hint. The weapon wheel's B cancel now compares the resolved key
instead of the east position; menus, prompts and binds already consume resolved keys.

`test_gamepad_labels.py` compiles the real helpers against test doubles for Xbox, PlayStation, Switch
labels, unknown and remapped labels, the environment opt-out and a missing pad, and checks the fallback
against SDL's own table: for Homebrew SDL 3.4.12 the Switch Pro and Joy-Con pair labels match it and Xbox
is positional.

### Keyboard

A probe of SDL 3.4.16's `SDL_GetKeyFromScancode` on the US layout: with key-event options, as SDL
produces `event.key.key`, A, Y, 1, / and keypad 1 gave the same keycodes with no modifier, Shift, Caps
Lock, both, and right Alt. Those are the unshifted layout codes SDL2 put in `keysym.sym`; without key-event
options, Shift, Caps Lock and right Alt changed them. `Key_MenuChar` therefore still applies Shift and Caps
Lock exactly once, and Shift+Y still answers Y/N prompts. The default `SDL_HINT_KEYCODE_OPTIONS`
(`french_numbers,latin_letters`) does differ from SDL2 on non-Latin layouts, whose letter keys now report
Latin keycodes, and on French layouts, whose number row reports digits. Neither layout was tested.

The Windows keyboard hook's synthetic events now carry the window ID, modifier state and the keycode SDL
would report; the macOS launcher's settings shortcut already did.

### Checked and unchanged

- Text input: `IN_Init` runs after `VID_Init` and applies the initial state to the real window, and the
  window is destroyed only at shutdown, so no recreated window needs the state reapplied.
- Wheel: bindings use whole `integer_y` steps, including several per event, and there are no horizontal
  wheel keys. Neither the SDL2 nor the SDL 3 code reverses `SDL_MOUSEWHEEL_FLIPPED` events, so natural
  scrolling keeps its direction.
- Drops: batched and single drops copy `event.drop.data` before use and never free it.
- Controllers: `joy_device` is a position in a fresh `SDL_GetJoysticks` list, the active pad is revalidated
  by instance ID on add, remove and remap events, enumeration arrays are freed, and the four paddles map
  to separate keys.
- The quakespasm#48 warp-before-relative-mode workaround exists only in an `#if 0` copy of `IN_Activate`
  that predates the migration; the live path does not warp. `SDL_HINT_MOUSE_RELATIVE_MODE_WARP` and the
  PS4/PS5 rumble hints were removed in Task 2.

`test_gamepad_labels`, `test_utf8_to_quake`, `test_menu_text_input`, `test_hid_start_wait` and
`test_mouse_motion` pass. macOS Debug, MinGW Win64 and MinGW Win32 compile the changes without warnings.

### Not covered

The physical gate: slow mouse movement, menu sliders, console selection, demo scrubbing, trackpad
scrolling, hot-unplug while holding a button, two-controller selection, a Switch Pro or Joy-Con next to
another pad, IME and non-US layouts. `test_mouse_sources.py` exists only with the main checkout's
uncommitted HID mouse work.

## Task 5: audio streams

The temporary `snd_null.c` and the capture stub are gone; every build compiles `snd_sdl.c` again.

### Playback

`snd_sdl.c` opens one stream on the default output. Its input format is what the engine paints: the
mix rate, unsigned 8-bit or signed 16-bit samples, and the device's channel count when `snd_surround`
is on. SDL converts that to whatever the device uses. `paint_audio`, the volume scaling and the
surround up-mix are unchanged; a new stream callback paints whole device frames into a preallocated
scratch buffer, in chunks no larger than one ring's worth of frames, and hands them to
`SDL_PutAudioStreamData`. The main thread's DMA lock is `SDL_LockAudioStream`, block and unblock pause and
resume the stream's device, and the stream is destroyed before the ring and scratch memory are freed.
A rejected put is recorded by the callback and reported once by `SNDDMA_Submit`.

The period and ring are engine policy rather than what the device reports: 256, 512, 1024, 2048 or
4096 frames for rates up to 11025, 22050, 44100 and 56000 Hz and above, and a ring of ten periods of
interleaved stereo rounded up to a power of two. The period is requested through
`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` at default priority, so an environment value still wins.
`SND_OpenAudioStream` owns that hint for playback and capture: capture asks for 256 frames and the
playback request is restored afterwards. The ring starts at the format's silence value, which SDL2's
`calloc` got wrong for unsigned 8-bit.

`SNDDMA_GetDeviceName` names the physical device behind the stream when asked, so it follows device
changes. Playback device added, removed and format-changed events refresh that name and, with surround
on, the up-mix channel count, resizing the scratch buffer outside the stream lock; the stream itself is
never reopened because SDL moves a default-output stream to the new default device.

### Capture

Voice capture opens a separate stream on the default recording device in mono signed 16-bit at the
codec's rate, starting and stopping by resuming and pausing it and clearing it each time so old speech
is never sent. `SDL_Capture_Update` returns nothing below `minbytes` or on a negative count, caps at
`maxbytes` and returns whole samples. The SDL capture driver is no longer behind `USE_SDL_CAPTURE`. The
DirectSound driver in the same file stays behind `USE_DSOUND_CAPTURE`, which no build defines, as
before the migration. `NSMicrophoneUsageDescription` is still in the macOS `Info.plist`.

### Verification

- `test_sdl3_audio.py` compiles the production paint path, stream callback, period and ring policy and
  `SDL_Capture_Update` against test doubles with address and undefined-behaviour sanitizers and guard
  bytes around the ring and scratch buffers. It checks the up-mix example from the plan, an eight-frame
  ring wrapping from `samplepos` 14 to 6 across twenty frames in frame order, partial and larger-than-ring
  requests, master volume, unsigned 8-bit silence, 16-bit and 8-bit 5.1 layouts and a rejected put, plus
  capture failure, empty, `minbytes`, `maxbytes`, alignment and read-error cases.
- An SDL 3.4.16 probe on the test Mac opened a default playback stream with the 1024-frame hint: the
  device ran at the requested 44100 Hz with 1024 frames and each callback asked for exactly one period.
- `python3 Misc/sdl3/audio_lifecycle.py <label> <QSS-M.app>` drives `snd_restart` over rcon and checks
  that sound restarts with the requested bits and rate and that the DMA cursor keeps advancing. Debug
  builds, arm64, MacBook Pro speakers:

| Step | SDL2 baseline | SDL3 |
|---|---|---|
| Startup, 16-bit 44100 Hz, surround on | OK, 65536-byte ring | OK, 65536-byte ring |
| Surround off | OK, 65536 | OK, 65536 |
| 8-bit 44100 Hz | OK, 32768 | OK, 32768 |
| 22050 Hz | OK, 32768 | OK, 32768 (device 44100 Hz, 512 frames) |
| 11025 Hz | OK, 16384 | OK, 16384 (device 44100 Hz, 256 frames) |
| 48000 Hz | OK, 131072 | OK, 131072 (device 48000 Hz, 2048 frames) |
| 96000 Hz | OK, 262144 | OK, 262144 (device 96000 Hz, 4096 frames) |
| Defaults again | OK | OK |
| Another app focused, refocused | OK | OK |
| Quit | clean | clean |

  Ring sizes and rates matched SDL2 at every step, so no obtained-size difference changed the ring. The
  SDL3 device line shows the period request honoured; below 44100 Hz the device stayed at 44100 Hz and
  SDL resampled.
- macOS Debug, MinGW Win64 and MinGW Win32 build the backend and capture code without warnings.

### Not covered

Live microphone capture: opening the recording device would raise the macOS microphone permission prompt,
so start/stop, denied permission, unplugging and loopback between two clients were not run. Also not
run: music plus effects by ear, surround hardware, pause and demo seeking, switching the output device
while playing, and onset latency against the SDL2 baseline measured with loopback or external capture.

## Task 6: builds, packaging and CI

### Changes

- `Windows/SDL2` and `macOS/SDL2.framework` (167 files) are removed after a check that no build file,
  script, workflow or source still referred to them.
- The Visual Studio project is now `quakespasm.vcxproj`, with the solution, `msvs.yml` and `.gitignore`
  following it and output under `Build-quakespasm`. `msvs.yml` still publishes `QSS-M-w64.exe` and
  `QSS-M-w32.exe`.
- `windows.yml` no longer installs the unused `libsdl2-dev`, and the MSYS2 diagnostics list SDL3 packages.
  Both MinGW paths build against the vendored SDL3 and ship its `SDL3.dll`.
- Dependency watch: SDL3 3.4.16 against `mingw-w64-x86_64-sdl3` 3.4.16-1, the MSYS2 revision on
  2026-09-13. MSYS2 has no i686 `sdl3` package (404), so only mingw64 is monitored. The DLL inventory
  root is `Windows/SDL3`.
- Linux CI: `ubuntu-latest` is Ubuntu 24.04, which has no SDL3 package (Launchpad's first `libsdl3` is
  3.2.8 in 25.04). `.github/scripts/build-sdl3-linux.sh` fetches exactly the commit behind the release
  tag — `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` for 3.4.16 and `5ac37a8ffcf89da390404c1016833d56e2d67ae4`
  for 3.2.12, resolved through the GitHub API — and fails unless SDL's configure summary reports X11,
  Wayland, ALSA, PulseAudio, PipeWire, HIDAPI, udev and D-Bus enabled. It installs a shared library into
  a prefix cached by version, commit, OS release and compiler. `linux.yml` builds against 3.4.16 and
  packages it, and separately compiles and links against 3.2.12 to catch newer API use. The SDL3 prefix
  leads the existing `PKG_CONFIG_PATH` and `PKG_CONFIG_LIBDIR` assignments.
- Linux packaging: `build-linux.sh` links with a `$ORIGIN` runpath and adds `libSDL3.so.0` to
  `QSS-M-l64.zip` when `SDL3_RUNTIME` names it. The package job unpacks the zip into a clean directory
  and checks with `readelf` and `ldd` that the executable resolves the bundled copy. `valgrind.yml` runs
  the engine against the pinned 3.4.16 runtime.
- Updater: Windows packages must carry `SDL3.dll`. `SDL3.dll` and `libSDL3.so.0` are staged, and both are
  copied beside the update helper, because on Windows and Linux the helper is a copy of the executable.
- SDL 3 reads `SDL_AUDIO_DRIVER` and `SDL_VIDEO_DRIVER`, not `SDL_AUDIODRIVER` and `SDL_VIDEODRIVER`; the
  docs, the valgrind script and the harnesses use the new names.
- Docs: `BUILDING.html` lists SDL3 packages for each Linux family, notes that `libsdl3-dev` needs Debian 13
  or Ubuntu 25.04 (with a source-build route otherwise), states the 3.2.12 minimum, the bundled 3.4.16 and
  the X11-first policy with `SDL_VIDEO_DRIVER=wayland`, and drops the MSYS2 SDL2 package. `Quakespasm.txt`
  and `.html` update their current-state SDL lines and keep the changelog. The macOS build instructions,
  stress README, UTF-8 note and codecs README follow; the vendored `SDL3.dll` imports no C runtime DLL,
  where `SDL2.dll` imported `msvcrt.dll`.

### Verification

- macOS Debug and a clean universal Release build succeed with `SDL2.framework` gone. The Release app embeds
  only `SDL3.framework`, has x86_64 and arm64 slices, adds no warning kinds over the SDL2 Release baseline,
  and passes `verify-macos-release.sh` on a staged archive.
- `build_cross_win64.sh` and `build_cross_win32.sh` link from a tree without `Windows/SDL2`, compiling the
  updater changes without warnings.
- `node --test .github/scripts/check-msys2-dependencies.test.js .github/scripts/notify-dependency-updates.test.js`
  passes 21 tests, and the manifest covers every bundled DLL.
- The edited workflows parse as YAML, the shell scripts pass `sh -n`, and a scratch Makefile showed the
  `$ORIGIN` runpath reaching the linker literally.

None of the GitHub workflows has run: the Linux SDL3 source builds, the bundled-runtime check, valgrind,
MSVC and MSYS2 are untested, as is running any Windows or Linux artifact. Haiku cross scripts have no SDL
references and were not built.

### Updating existing installs

The updater in builds before this migration applies only files from its own list. On Windows it rejects an
SDL3 package for lacking `SDL2.dll`, so those users must download the SDL3 release manually. On Linux it
would install the new executable without `libSDL3.so.0`, which fails to start on systems without SDL3, so
Linux users must also update manually and must not use the old in-game updater for this release. macOS
replaces the whole app bundle and updates normally. The chosen rollout is a manual update for Windows and
Linux; builds from this migration onward know the SDL3 runtime files.

## Task 7: integrated validation

Only the macOS arm64 part of the validation matrix could run on the test Mac. Windows, Linux X11 and
Wayland, the x86_64 slice, controllers, keyboard layouts, IME, mixed-scale and hot-plugged displays,
microphone use, the launcher's Finder/Dock/badge paths and physical input latency were not exercised and
count as missing coverage, not passes.

### Builds under test

Products were resolved from `xcodebuild -showBuildSettings` (`TARGET_BUILD_DIR` plus `EXECUTABLE_PATH`) at
revision `2b3c8cd88`, and those exact executables were used for every run below:

| Build | Executable SHA-256 | Slices | Links |
|---|---|---|---|
| Debug | `edd495f321509f9d68c450877a311ef325aa881bc89352e6c841a6d064ba84c9` | arm64 | `@rpath/SDL3.framework/Versions/A/SDL3` |
| Release | `d1e048891b35f88ae3093e82253ccb1188b9148043368f5cd1e336d90ed808de` | x86_64, arm64 | `@rpath/SDL3.framework/Versions/A/SDL3` |

The framework embedded in both apps hashes `e15e633d98337d24…` rather than the vendored `546ce8509e3ead9e…`.
Both copies are ad-hoc signed; Xcode's `CodeSignOnCopy` re-signs the embedded one. Its `__text` disassembly,
symbol table and 1271 exports are identical to the vendored framework on both slices; only `__LINKEDIT`,
which holds the signature, is 16 KiB smaller per slice. `COPY_PHASE_STRIP` is off, so nothing was stripped.
`macOS/build-macos.sh` was not run, because it rebuilds the vcpkg tree shared with the main checkout; the
Release app was staged and checked with `verify-macos-release.sh` instead.

### Video mode cycles

```sh
python3 Misc/sdl3/vid_transitions.py <label> <QSS-M.app> --cycles 10
```

On the Release build, ten cycles of desktop fullscreen, windowed 800x600, exclusive 1800x1125, windowed
640x480, desktop fullscreen and windowed 1024x640 confirmed 60 of 60 transitions: every settled mode matched
the request and the immediate report after `vid_restart`, with no refused requests or fallbacks, and the game
exited cleanly. The plan asks for 100 cycles per backend; only 10 ran, on one built-in display, which keeps
the screen for about seven minutes per run.

### Launch and quit timing

Release builds, arm64, fresh processes launched with `-window -width 640 -height 480 +quit`, every console
line timestamped from spawn (a scratch timing script; five interleaved runs per build, medians):

| Run | SDL2 ready | SDL2 total | SDL3 ready | SDL3 total |
|---|---|---|---|---|
| `-nosound`, empty basedir | 0.296 s | 0.500 s | 0.377 s | 0.487 s |
| `-nosound`, benchmark basedir | 0.315 s | 0.503 s | 0.386 s | 0.494 s |
| sound, benchmark basedir | 0.468 s | 0.750 s | 0.522 s | 0.786 s |

Launch through exit takes the same time without sound. "Quake Initialized" arrives about 80 ms later
because `VID_SetMode` now waits in `SDL_SyncWindow` for the window to appear (137 ms from the heap line to
the video mode line, against 52 ms), where SDL2 left that work until after initialization (a 130 ms gap
before `quake.rc` that SDL3 no longer has). Any measurement that splits launch from quit at that line moves
the same time between them. With sound, SDL3 initializes CoreAudio about 23 ms faster but the process takes
about 80 ms longer to exit after "Shutting down SDL sound" (201 ms against 121 ms). `-nojoy` changes nothing.

Standalone probes against the two frameworks put the SDL-level differences in the same places: gamepad
subsystem init and quit match (12–17 ms and 0.1 ms on both), video subsystem quit costs 21 ms on SDL3
against 0.2 ms, and `SDL_SyncWindow` after showing a window costs 35 ms but shortens the first frame by
about 13 ms.

### Benchmark

```sh
python3 Misc/sdl3/bench.py results.json baseline=<SDL2 QSS-M.app> sdl3=<SDL3 QSS-M.app>
```

Release builds, baseline `25108a2fe` against the Release build above, runs interleaved:

| Measurement | SDL2, 5 runs | SDL3, 5 runs | SDL2, 3 runs | SDL3, 3 runs |
|---|---|---|---|---|
| timedemo demo1, 1280x800 (fps) | 1196.6 | 1201.5 | 1098.8 | 1134.9 |
| Launch to "Quake Initialized" (s) | 0.444 | 0.555 | 0.449 | 0.514 |
| Quit to exit (s) | 0.288 | 0.580 | 0.281 | 0.289 |
| Launch to ready, `-nosound` (s) | 0.305 | 0.389 | 0.289 | 0.388 |
| Quit to exit, `-nosound` (s) | 0.201 | 0.411 | 0.195 | 0.102 |
| CoreAudio init (s) | 0.138 | 0.166 | 0.159 | 0.126 |
| CoreAudio shutdown (s) | 0.087 | 0.169 | 0.087 | 0.186 |

Frame rates are within run-to-run spread. The first run's SDL3 quit times, 200 to 300 ms slower, did not
reproduce in the second run or in the timestamped launches above, where launch through exit takes the same
time without sound; they are treated as noise. What does reproduce is the ready line moving about 80 to
100 ms later, which only shifts time out of the quit measurement, and audio shutdown taking about 100 ms
longer at exit, so a launch and quit with sound costs about 70 ms more than under SDL2.

### Engine workloads

```sh
python3 Misc/sdl3/engine_workloads.py <label> <QSS-M.app>
```

In a scratch basedir the script drives a windowed listen server over rcon, plays the recorded demo from the
command line and runs a dedicated server, judging each step from the files left behind, the console, and
for the dedicated server the map name returned by a server-info query. Both Release builds passed all 15
checks: the listen server recorded a demo into `id1/demos`, saved and loaded a game, wrote a screenshot
through the worker thread, changed map to e1m1, stayed responsive and quit cleanly; the demo played back;
the dedicated server reported map `start` and exited on SIGTERM with status 143 (128 + SIGTERM, the
engine's signal exit); no engine errors were logged. The playback fps (719 on SDL3, 859 on SDL2) comes from
a demo lasting under half a second and is not a performance measurement.

### Harness suite

On the final tree, `test_alias_visual_probe`, `test_bsp_miptex`, `test_chunked_download`,
`test_console_completion`, `test_demo_list`, `test_gamepad_labels`, `test_harness_hardening`,
`test_hid_start_wait`, `test_hotpath_lookups`, `test_map_categories`, `test_menu_text_input`,
`test_mouse_motion`, `test_quit_http`, `test_sdl3_audio`, `test_teleport_scissor`, `test_tick_deadlines` and
`test_utf8_to_quake` pass (17 of 17). Not run: `test_bsp_lump_bounds`, `test_msgread_bounds` and
`test_suggestion_cap` need a `QSSM_STRESS` build; `test_chunked_download_udp` and `test_teleport_scenecache`
need `--pak`/`--basedir` inputs; `test_quit_udp` and `test_teleport_gl` compile against SDL 3 but link only on
Linux.

### Residual SDL2 scan

The plan's searches find no `USE_SDL2`, `SDL_VERSION_ATLEAST(2`, `SDL_INIT_CDROM`, `SDL2main` or `sdl2-config`
in engine sources, macOS sources and configurations, workflows or build scripts, and no `SDL2`, `sdl2` or
`cd_sdl` in the Makefiles, Visual Studio project, Xcode project, release verifier or workflows. Engine
sources mention SDL2 only in comments that describe history or a compatibility contract; the two that
read as current behaviour (the gamepad key comment in `keys.h` and the timer description in `sv_main.c`)
now say SDL. `git diff --check` across the migration reports only `quakespasm.sln`, whose CRLF ending is
unchanged from the base revision, and `Quake/pr_ext.c` has the same diff with or without
`--ignore-cr-at-eol`.

### Harnesses not run

- The plan's `Misc/stress/test_sdl3_video_cycles.py` needs a stress build. `Misc/stress/host_stress.c` is
  tracked but not compiled, and `host.c` no longer carries the `QSSM_STRESS` hooks, so a stress target would
  mean restoring archived instrumentation. The cycle gate below uses `vid_transitions.py --cycles` instead:
  `vid_describecurrentmode` reports SDL's live window size and fullscreen flag, not cvars, though it does not
  report the display or framebuffer size a `_stress_video` query would.
- `Misc/csqcharness/run_harness.py` needs `fteqcc` to rebuild its progs against the binary under test, and
  there is no `fteqcc` on the test Mac.

## Follow-up review: 2026-09-14

Reviewed migration commit `55f487e68` on `claude/qss-m-sdl3-migration-75a55a` and applied these fixes in the
same worktree:

- Voice capture now pauses and flushes its stream when recording stops. Clearing it discarded speech
  still queued beyond `S_Voip_Transmit`'s 32 KiB buffer, and flushing also makes the resampler tail available
  for the existing drain. Starting a new recording still clears old samples. This follows
  [SDL_FlushAudioStream's documented behavior](https://wiki.libsdl.org/SDL3/SDL_FlushAudioStream).
- Windows and Linux release scripts recreate their ZIP before packaging. Updating an existing ZIP kept
  obsolete SDL2/codec files and, for Linux system builds, a previously bundled SDL3 runtime.
- The shipped text and HTML instructions now explain the manual first SDL3 upgrade on Windows/Linux.
  Removed the obsolete `-cddev` hint and an SDL2-era clipboard return-value comment.
- Runtime checks now count audio shutdown in the result, verify save/load by leaving the saved map and
  checking that loading restores it, query the actual map after transitions, detect load errors, and
  require the dedicated server's expected SIGTERM exit status.

### Verification of the review fixes

```sh
python3 Misc/stress/test_sdl3_capture_lifecycle.py
python3 Misc/stress/test_release_archives.py
python3 Misc/stress/test_sdl3_audio.py
```

The new capture test failed before the fix and passes with real SDL3 streams under ASan/UBSan: it drains
60,000 bytes across the 32 KiB boundary, discards old samples on restart, and retains the complete 48 kHz
to 16 kHz resampler tail. Only device pause/resume are replaced; it opens no microphone. All four release
archive fixtures failed before the fix and pass afterward, using the real packaging scripts and ZIP tool
with compilation mocked. The existing playback/capture harness also passes after the fix.

All 17 standalone harnesses listed under Task 7 passed during this review, as did the 21 dependency-script
tests. The localhost HTTP/harness tests required execution outside the filesystem sandbox. Shell syntax,
Python syntax for the edited runtime tools, and `git diff --check` pass.

Fresh macOS Debug and universal Release builds succeed. The incremental Debug rebuild introduces no
warning kinds compared with the fresh pre-fix Debug build. The Release executable has x86_64 and arm64
slices and SHA-256 `660ee049c4a90c59ecf912101f553ee2abf90a9da2fdbe034f9800db5cb88f95`. Its product path was
resolved from `xcodebuild -showBuildSettings`; that exact app passed:

- `macOS/verify-macos-release.sh` on the app and a staged ZIP, including architecture, deployment target,
  embedded SDL3, signature and archive contents checks.
- `Misc/sdl3/audio_lifecycle.py review-20260914 <app>`: 11/11, including sample bits/rates, focus changes and
  clean shutdown on CoreAudio.
- `Misc/sdl3/engine_workloads.py review-20260914 <app>`: 16/16, including demo record/playback, save/load,
  screenshot output, map changes and dedicated-server startup/shutdown.

Build logs and runtime summaries are under `/tmp/qssm-sdl3-review-20260913` on the review Mac. Tests used
scratch game directories. Windows/Linux runtime, physical microphones/controllers, alternative
keyboard layouts/IME and multi-display behavior were not exercised in this follow-up; Task 7's remaining
platform and hardware coverage gaps still apply.

### Build and CI audit before landing

- Fixed the MSVC workflow's missing `quakespasm.pak` and made archive validation require the executable
  and both PAKs. The updater requires these files, as do the other release packages.
- Linux CI now runs the audio adapter, real SDL3 capture lifecycle, and release archive regression tests
  against both the supported minimum SDL 3.2.12 and the pinned SDL 3.4.16.
- `actionlint` 1.7.12 validates all workflows (optional ShellCheck/Pyflakes integrations unavailable).
  All 20 tracked shell scripts pass their shell's syntax check; dependency tests pass 21/21 and the
  dependency manifest covers the packaged DLLs. Both pinned SDL sources' CMake option summaries match
  the Linux backend verification expression.
- Both root Windows release scripts compile and package successfully in isolated scratch copies with
  MinGW-w64. The ZIPs contain both PAKs, the controller database and SDL3 runtime, contain no SDL2 runtime,
  and the executables import SDL3 with the Windows GUI subsystem. These local cross builds use
  `MAKEARGS='-j8 USE_GNUTLS=0'`; the hosted Windows jobs separately require GNUTLS.

## Post-migration frame pacing

- The client FPS limiter shares its interval calculation with the frame filter and
  uses `SDL_DelayPrecise` for the remaining interval. It accounts for rendering,
  vsync and time accumulated by rejected frames. The filter now retains the main
  loop's double precision. Existing `sys_throttle` opt-outs, uncapped throttling,
  timedemo behavior and dedicated-server scheduling are preserved. Synthetic lag
  limits waits to 1 ms so queued moves still get frequent service.

Validation on Linux with the supported minimum SDL 3.2.12:

- Full engine build succeeds. The new frame-pacing test runs in Linux CI.
- `test_frame_pacing.py` covers capped/uncapped play, menu refresh rates, throttle
  opt-outs, timedemos, overruns, partial intervals, synthetic lag and sustained
  cadence at 60/120/144/250/500/1000 FPS.

`test_frame_pacing.py --benchmark` compares real SDL timers with 0.3 ms of
simulated render work per frame. One local run (two seconds per case):

| FPS cap | Mean interval, old / precise (ms) | 99th percentile, old / precise (ms) | CPU, old / precise (% of one core) |
|---|---|---|---|
| 144 | 7.895 / 6.958 | 8.022 / 7.021 | 2.21 / 2.99 |
| 250 | 4.684 / 4.017 | 5.010 / 4.080 | 2.27 / 3.26 |
| 500 | 2.534 / 2.016 | 2.741 / 2.087 | 2.70 / 4.29 |

This timer benchmark does not measure GPU rendering or input-to-display latency.
Precise waiting can spend more CPU to meet the requested deadline. Windows/macOS frame pacing still needs hardware testing.
