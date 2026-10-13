Live MSAA
=========

Changing `vid_fsaa` in the console or graphics menu applies on the next frame
without reconnecting, reloading the map, or restarting video. `0` disables MSAA.
The menu offers 2x, 4x, 8x, and 16x requests up to the driver's sample limit.
When the driver rounds a request, the menu shows `requested (applied)`, such as
`2x (4x)`. `vid_describecurrentmode` also reports both counts. Allocation failures
fall back to fewer samples or off.

MSAA and FXAA are mutually exclusive. Enabling either disables the other and
prints a short console note. When both are enabled in startup config, FXAA takes
precedence; an explicit `-fsaa` command-line setting overrides that config.

MSAA now requires multisample framebuffer and blit support, packed depth/stencil,
and non-power-of-two textures. Older GPUs that previously provided MSAA through
the SDL window may lose MSAA with this release. There is no startup-only window
MSAA fallback; ordinary rendering remains available.

High sample counts consume substantial video memory. At 3840x2160, 16x color
and depth/stencil buffers alone occupy about 1013 MiB, before resolve textures,
other rendering resources, driver overhead, or temporary replacement buffers.
The engine warns when these buffers reach 512 MiB. Lower `vid_fsaa` if enabling
it causes stutter, even when allocation succeeds.

Motion blur and render scaling/underwater warp each need an additional full-frame
MSAA resolve when copying scene color. Hardware timedemo comparisons at 0x and
4x, with those effects enabled separately, are still required before release.

Outstanding platform checks include Windows/macOS hardware, macOS gamma ramps,
real resize/HiDPI/fullscreen/minimize/restore, successful rendering on an EXT-only
framebuffer driver, and screenshots between frames on macOS/Wayland. The existing
screenshot path reads the window after presentation; offscreen Linux testing
does not validate backbuffer contents on those platforms.
