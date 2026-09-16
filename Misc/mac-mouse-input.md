# Mac mouse input verification

Mac raw mouse input now uses one source for gameplay motion. Previously, when
HID initialized successfully, both HID and SDL movement could turn the view.
Removing that combination changes sensitivity by an amount that depends on the
device and system settings; there is no automatic sensitivity conversion.

Mouse Options now calls this setting **Raw Input**, with **Off**, **Ready**, and
**Unavailable** states. Ready means HID initialized, not that the current device
has delivered movement. `in_mouseinfo` shows the selected gameplay backend,
Input Monitoring status, initialization errors, and observed HID aim batches.
SDL pointer acceleration is reported as unmeasured.

- `in_disablemacosxmouseaccel 2`: request HID raw aim. If HID cannot initialize,
  use ordinary SDL input and report Unavailable. If HID initializes, it remains
  the only source for aim, including after idle periods.
- `in_disablemacosxmouseaccel 0`: use ordinary SDL input immediately. Use this
  for trackpads or pointers that the HID mouse backend does not report.
- `in_disablemacosxmouseaccel 1`: the existing legacy system-acceleration path;
  it is not selected automatically after a raw-input failure. Its system-setting
  changes and restoration need separate verification on current macOS.

The menu toggles 0 and 2. Raw input does not switch automatically to SDL after
an idle timeout: timing alone cannot distinguish a second device from a delayed
copy of the same movement. Pending movement is cleared when changing modes.
Absolute HID coordinates are ignored. Cursor, buttons and scrolling use SDL.

## Hands-on check before release

Use the exact build being tested. If choosing to test HID, grant that app Input
Monitoring in System Settings > Privacy & Security, then quit and relaunch it.
A different build or signature may require a separate grant. Permission is an
optional player choice; ordinary input remains available.

1. Open a local map, run `in_mouseinfo`, then `in_mousesources 1`. Close the
   console before moving: HID aim batches are counted during gameplay only.
2. Make five slow and five fast straight strokes in one direction, pausing at
   least half a second between them. Run `condump` afterward to retain results.
   Confirm HID counts increase; initialization alone is insufficient.
3. Compare the reported SDL/HID displacement ratios. Under the assumption that
   the older build summed those same displacements, the old/new gain ratio is
   approximately `1 + SDL/HID`. This is a setup-specific estimate, not a
   cross-platform calibration. Curved or reversing strokes invalidate it.
4. Test starting after idle, repeated strokes, opening/closing the menu,
   losing/regaining focus, and changing maps. Watch for jumps or lost movement.
5. Click Raw Input off and on in Mouse Options and check `in_mouseinfo` after
   each click. Movement from the old mode must not jump the view afterward.
6. Repeat with a trackpad. If HID does not report it, turn Raw Input off and
   verify ordinary input responds immediately, including directly after using
   the mouse. Automatic switching between devices is deliberately unsupported.

`python3 Misc/stress/test_mouse_sources.py` exercises production routing with
synthetic inputs and sanitizer checks. `test_menu_text_input.py` checks shared
menu input behavior. Neither measures physical input latency or mouse scaling.
A before/after click-to-photon result still requires a separate physical test;
these changes do not establish a macOS-versus-Windows/Linux latency difference.

## SDL3 migration

SDL3 3.4.16 still delivers Cocoa mouse motion on macOS, so QSS-M's separate
HID reader still needs exclusive routing. SDL3 motion and cursor coordinates
are floating point: the normal input path, suppressed SDL diagnostic samples,
and stroke totals preserve those fractions. HID reports remain integer counts.
The SDL3 HID startup state and condition-wait handling are retained.

Run `test_mouse_motion.py` for fractional movement and QC cursor behavior and
`test_hid_start_wait.py` for startup timeouts and early condition wakeups, in
addition to the routing tests above. These use synthetic input, not a physical
mouse or click-to-photon measurement.

### Verification on September 15, 2026

The SDL3 adaptation passes `test_mouse_sources.py` (Mac and fallback paths,
ASan/UBSan), `test_mouse_motion.py`, `test_hid_start_wait.py`,
`test_menu_text_input.py`, and `test_console_completion.py`. The macOS Xcode
Debug target builds successfully. Windows and Linux application builds were
not run; the fallback test compiles the non-Mac input path with test doubles.

A freshly built Debug app launched through LaunchServices using an isolated
`/tmp/qssm-sdl3-mouse-runtime` basedir. Local rcon verified diagnostics, mode
`2 -> 0 -> 2`, a map change from `start` to `dm4`, and clean quit. The Desktop
play app and checked config checksums were unchanged. Runtime output is in
`/tmp/qssm-sdl3-mouse-smoke.log`; the build log is
`/tmp/qssm-sdl3-mouse-debug.log`.

On that launch, `IOHIDManagerOpen` returned `0xe00002e2` and Input Monitoring
was Denied. Only ordinary SDL fallback ran. Physical HID movement, the menu
click, device sensitivity ratios, and click-to-photon latency are still
unverified. No latency improvement is claimed by these checks.
