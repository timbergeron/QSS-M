# Windows startup and shutdown, 2026-10-04

Startup overlaps SDL audio device opening and replacement-image decoding with
window creation. Windows warms the OpenGL pixel-format path in the background
and postpones DirectInput enumeration without dropping legacy-controller support.
Display modes are enumerated on demand. Gameplay shaders compile when a map needs
them; FXAA initializes on its first enabled frame and retries failed compilation
only after a video restart.

Shutdown hides the window when host teardown starts and overlaps audio-device
closure with persistence and other cleanup. Audio callbacks retain their buffers
until closure finishes. Config, history, IP logs, and stale-download cleanup remain
part of normal shutdown. Temporary timing hooks and automatic-quit experiments
have been removed from the engine sources.

## Measurements

Same Windows PC, Release x64, NVIDIA OpenGL driver 591.86, menu startup with
24-bit HUD replacements and a sanitized copy of the existing config. Quit fade
was disabled for latency measurements. These are repeated launches with warm OS
and driver caches, not cold-boot results.

| Measurement | Interrupted version | Finished version |
| --- | ---: | ---: |
| Process creation to first presented frame, median | 809 ms (5 runs) | 406 ms (7 runs) |
| Menu quit to process exit, median | 150 ms (5 runs) | 142 ms (7 runs) |

The original session reported approximately 1,120 ms before its initial
optimizations. The comparison above measures the already optimized interrupted
version against this continuation. Graphics-driver/window work remains the largest
startup cost. Deferring gameplay shaders moves their cost to map loading.

A normal window-close test after map loading and audio/video restarts, using the
final binary without profiler hooks, hid the window in 4.7 ms and exited in
146.7 ms. The 32-bit test with sound and DirectInput explicitly disabled measured
7.6 ms and 144.2 ms respectively. User-selected quit fades still add their duration.

## Reproduction and checks

Build `Windows/VisualStudio/quakespasm.vcxproj` with Release x64 or Win32.
Both configurations built successfully. No new warnings were reported for the
changed code; the full Win32 build retains unrelated signedness warnings.

From a Windows checkout with Visual Studio C++ tools installed:

```bat
Misc\sdl3\startup_audio_test.cmd
Misc\sdl3\startup_regression_test.cmd
python Misc\sdl3\windows_lifecycle.py path\to\quakespasm.exe --paks path\to\id1 --label release-x64
python Misc\sdl3\windows_lifecycle.py path\to\quakespasm.exe --paks path\to\id1 --label early-quit --immediate-quit
python Misc\sdl3\windows_lifecycle.py path\to\quakespasm.exe --paks path\to\id1 --label no-audio --nosound
```

The audio regression test uses real SDL threads and the dummy audio device. It
covers delayed asynchronous open/close, callback progress, shutdown during a
pending open, open failure, and eight synchronous restarts with different rates
and sample formats. The runtime harness copies the executable and DLLs into an
isolated directory, links game PAKs read-only, and checks display mode enumeration,
the video menu, disconnected/connected video restarts, rendered maps, FXAA,
sound restarts, saved configuration, download-temp cleanup, and window-close exit.

Artifacts are written under `.codex-build`. Physical legacy-controller hotplug,
graphics-driver failure, and macOS/Linux runtime behavior were not tested on this
Windows host. Controller fallbacks and thread/resource ownership received source
review, including explicit SDL environment overrides and controller selection
across subsystem restarts.

## Production audit

The follow-up audit fixed shader ownership during video restarts: alias and world
program handles are now cleared before menu/gamma programs can reuse their OpenGL
IDs. A regression forces ID reuse over eight restart cycles.

The fallback-renderer check also reproduced a pre-existing map crash in the saved
baseline with `-noglsl`: scene caching admitted shader-only batches when VBOs were
available but GLSL was disabled. Queueing and drawing now require the complete
world/water shader paths before replacing standard texture chains. The Win32
runtime passed with GLSL and OpenGL prewarming disabled after this fix, and
admission tests cover disabled GLSL, missing world/water programs, and missing VBOs.

Controller discovery now requires usable equivalent gamepads, preserves the saved
index while initial discovery is pending, and restores selection across backend
GUID changes. Explicit SDL hint priorities are respected. Watcher overflow,
failed timers, and message-loop errors fall back to normal SDL enumeration.
A failed DirectInput subsystem transition restores the working backend and retries
only after a later native scan. Tests cover these cases with injected inventories
and failures; identical pathless devices still cannot provide unique physical
identity after reordering.

The audit also removed a race from the audio regression test and bounded the
runtime harness's wait for window hiding. Audio tests now inject subsystem,
allocation, stream-open, resume, and worker-creation failures. The shader and
controller tests run under AddressSanitizer through `startup_regression_test.cmd`
(Visual Studio C++ tools and Python required; set `QSSM_TEST_PYTHON` if needed).
The broader Windows pass covered eleven relevant stress scripts: audio playback,
capture lifecycle, physical playback device reopening, video modes/menus, frame
pacing, mouse accumulation, gamepad labels, tick deadlines, shader ownership,
controller discovery, and HTTP shutdown cancellation against a stalled local server.
Existing GNU compiler harnesses were adapted to MSVC for this pass; this does not
establish Linux/macOS runtime coverage or UBSan coverage on Windows.

Release x64, Release Win32, and Debug x64 builds passed. Runtime checks exercised
normal audio, disabled audio, injected audio-driver failure, early quit, maps,
connected/disconnected video restarts, FXAA, persistence, and temporary-file cleanup.
Desktop and exclusive fullscreen transitions passed as well.
Import and updater command-line self-tests also passed. Physical legacy controllers
and other operating systems remain outside this host's verified coverage.

The final normal Release x64 run hid its window in 7.4 ms and exited in 167.4 ms.
Dedicated-server early quit and an explicit DirectInput opt-out also passed.

Additional runtime variants:

```bat
python Misc\sdl3\windows_lifecycle.py path\to\quakespasm.exe --paks path\to\id1 --label fullscreen --fullscreen
python Misc\sdl3\windows_lifecycle.py path\to\quakespasm.exe --paks path\to\id1 --label failed-audio --audio-failure
python Misc\sdl3\windows_lifecycle.py path\to\quakespasm.exe --paks path\to\id1 --label no-glsl --engine-arg=-noglsl
```
