`scr_showgrenadecounter` selects a grenade fuse indicator:

| Value | Effect |
| --- | --- |
| `0` | Off (default) |
| `1` | Center-screen bar |
| `2` | Small bars above grenade models |
| `3` | Pulsing orange/red tint and soft halo on grenade models, without bars |

Mode `3` preserves the grenade's texture detail. Its pulses accelerate from
about 1 to 5 per second over the standard 2.5-second fuse, becoming brighter
and redder toward detonation. Each grenade follows its own fuse; pausing freezes
the effect. The tint and halo respect world depth and fog. The halo requires a
stencil buffer; the surface tint works without one. This mode uses alias-model
shaders, including supported animated replacement models.

Like the existing bars, this requires a local server (single-player or hosting)
and applies to the viewed player's own grenades. Remote servers and demos do not
provide the authoritative fuse information needed for this effect.

`cl_r2g` is supported: real grenades retain their fuse glow with rocket-to-grenade
model replacement enabled. Rockets using the grenade model do not receive a
fuse glow, since they explode on impact.
