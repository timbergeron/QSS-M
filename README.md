QSS-M is a open source port of the original ID Software Quake. Over 25 years Quake 1.09 became Fitzquake to Quakespasm that lead to Quakespasm Spiked and now Quakespasm Spiked Multiplayer (QSS-M). NetQuake competitive multiplayer clients started with ProQuake, transitioned to Qrack or Mark V, and now QSS-M made possible by all those voluntary efforts.

https://qssm.quakeone.com

Credits:
JPG, r00k, Spoike for QSS and FTEQW, Ozkan Sezer & Eric Wasylishen (Quakespasm), John Fitzgibbons (FitzQuake), Baker (MarkV), MH, Joe (JoeQuake), Andrei Drexler (Ironwail), & [many more](https://q1tools.github.io/tree/)

<details>
<summary>Ironwail Ports & Adaptations</summary>

**Input, Window, and Display**

- Advanced controller support: gyro, rumble, flick stick, hot-plugging, alt-modifier binds, controller menus, and test menus
- Current desktop resolution is used on first launch instead of 800x600
- Automatic UI scaling is selected from the first-run resolution
- Window resizing support with saved window size (`vid_saveresize`)
- Mouse input is ignored until the client is ready, preventing loading/signon misfires
- Mouse sensitivity precision fix for non-integer sensitivity values
- Windows Caps Lock interception and extended scan-code fixes
- Lost-focus key hook fix for smoother multi-instance play
- Startup window raise/focus fix
- Built-in zoom controls with adjusted mouse sensitivity
- Game and server summary in the window title
- Print Screen screenshot support

**Console, Commands, and Completion**

- Console text selection, copy support, selected-text highlighting, and word-by-word cursor movement
- Windowed-mode clickable console links for generated files and folders
- Fading console notifications
- Multi-column tab-completion output
- Search-match highlighting in console output, tab completion, menus, and texture filters
- Natural sorting for map and command-completion lists
- Tab completion for commands, cvars, aliases, `writeconfig`, `bind`, and `unbind`
- Tab completion for files, maps, demos, sounds, models, screenshots, and classnames
- Tab completion for video, texture, fog, sky, HUD, crosshair, color, player, and rendering cvars
- Tab completion inside quoted command arguments and bind strings
- `writeconfig` support with safer `.cfg` filename handling
- Easier command discovery with `find`/apropos behavior improvements
- Updated `version` command output
- `imagelist` and `imagedump` filtering

**Menus, Servers, and Map Browsing**

- Mouse support for classic menus, including sliders and menu navigation
- Options/menu value display improvements
- Menu live previews for visual options, including Shift pinning (`ui_live_preview`)
- Long-name scrolling and search highlighting in menu lists
- Mods, Levels/Maps, and Skill menus
- Map descriptions and cached map metadata
- Autoload last save on map restart

**Demos and Gameplay**

- Demo playback controls: pause, rewind, fast-forward, speed changes, frame stepping, and position jumps
- Demo playback overlay with configurable timeout (`scr_demobar_timeout`)
- Demo rewind keeps sounds, particles, beams, dynamic lights, stats, and screen color flashes in sync
- Underwater sound filter (`snd_waterfx`)
- `nomonsters 1` support when starting maps
- Savegame pitch-angle correction

**Rendering, Debugging, and Compatibility**

- Centerprint background styles (`scr_centerprintbg`)
- Sky wind support (`r_skywind`)
- `r_showbboxes` model-type coloring and filtering
- PVS-aware entity checks for more accurate bounding-box debug overlays
- `r_showfields` entity overlay inspector
- Expanded `viewpos` output with camera position and sun angle
- Status bar and intermission fixes for large monster counts
- Higher alias model frame/mesh limits for large mods
- Corrected viewmodel interpolation for >10Hz animations
- Fixed upright sprite rendering with Dutch-angle cameras
- Signon buffer fragmentation and server buffer crash fixes for large maps
- Larger single-player datagram buffer for complex maps
- Particle trail emission-rate limiting
- Particle trail state tracking for cleaner demo playback and rewinds
- External HUD/menu texture clamp and padding fixes to reduce edge bleeding
- Dynamic multi-segment hunk allocator so large maps load without needing `-heapsize`
- `Hunk_AllocNoFill` skips zero-fill on hot allocation paths
- Out-of-memory and size-overflow guards on previously unchecked malloc sites

</details>


[ALL QSS-M Commands & Variables (Google Sheets)](https://docs.google.com/spreadsheets/d/1ubOuromaXpZonfL-eJ-KA7q-xSRiBBuSvxahzF-uFOY/edit?usp=sharing)

```
timbergeron@gmail.com | discord.quakeone.com (woods#3451)
```
