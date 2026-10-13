# CSQC parity: highest-return FTE ports

Audit date: 2026-10-07. QSS-M source: `575f429a420d47bcc9f7bc669eb9eab0afbbe460`.
FTE reference: `f937b9d88f71fc4429db5fe56c6a98d922711b2e`, the official GitHub
mirror's master commit returned during this audit. This is a source comparison
with targeted QSS-M runtime probes, not a measured percentage of compatible mods.

## Implementation status

All six items in today's core set are now implemented in this worktree. The gap
analysis below describes the baseline commit named above. Validation completed
on Linux with SDL offscreen/Mesa llvmpipe: the engine builds, all three QC programs
compile with zero warnings, and the expanded harness passes **797/797 checks**.
HUD and MenuQC additive/line screenshots pass **48/48 pixel assertions** each;
four full-CSQC screenshots pass **49/49** each (UI-only and rendered frames at
scales 1 and 2), alongside the existing rotation grid.
See [the harness coverage notes](README.md#fte-parity-regression-coverage-2026-10-07).
Detailed logs and binary/input hashes are in
`.codex-build/csqc-derisk-20261007/artifacts-derisk/summary.json` and its sibling
logs. The artifacts moved out of `/tmp` after it filled while writing captures.

Full-CSQC testing also exposed missing 2D state setup before `CSQC_UpdateView`;
that path now initializes blending, alpha test, depth/cull state and texture
modulation before entering QC. It is tested both before and after `renderscene`.
The review found and repaired four further issues: distinct failed optional
model loads exhausted the global model-name table; entity token parsing lost
escapes and split punctuation incorrectly; full CSQC leaked its scissor into
engine overlays; and a truncated MDL header was read before its size was checked.
Regression coverage includes 9,000 distinct load failures, same-process model
recovery, malformed model rejection and owned token input across a VM reload.
The actual token parser also passed 100,000 random streams under ASan/UBSan.
This was a targeted parser sanitizer run; a complete engine sanitizer run and
native GPU/macOS validation remain outstanding.
The unrelated follow-up items remain outside this implementation.

## Recommendation

**Today's order: frame-query fix, CSQC `getmodelindex`, `getentitytoken`,
`bufstr_find`, `drawline`, then additive drawing flags.** This revision separates
the small frame-query repair from precache lookup work, promotes the confirmed
entity-token bug into the core set, and puts the renderer-state change last.
These changes need no new protocol, VM bytecode format, asset format, or renderer.

| Order | Port or repair | Existing gap | Practical benefit | Rough effort, including focused checks |
| --- | --- | --- | --- | --- |
| 1 | Signed model handles in the frame-query trio | Every negative model handle is rejected before resolution | Named animation queries work for client-only models | 10–20 minutes |
| 2 | CSQC `getmodelindex` #200 with `queryonly` | Only MenuQC has a handler | Model lookup without temporary entities or precache-slot corruption | 45–75 minutes |
| 3 | `getentitytoken(custom_string)` and owned input lifetime | Every reset argument selects the map entity lump | Custom text parsing follows the documented reset API | 20–30 minutes |
| 4 | `bufstr_find` #537 | Entry is commented out despite the rest of the string-buffer API existing | Search/filter code can use FTE's API directly | 0.5–1 hour |
| 5 | `drawline` #315; MenuQC #466 | Entry is commented out | HUD graphs, crosshairs, connectors, and debug overlays need no polygon workaround | 0.5–1 hour |
| 6 | `DRAWFLAG_ADD` across existing 2D draw calls | Flag arguments are ignored by pictures, subpictures, text, and fills | HUD glows and additive indicators render as requested | 1–2 hours |

Allow about **4–6 hours with focused tests** for this core set. These are planning
estimates from the inspected code and review, not implementation measurements;
broader platform checks and review are additional. The dynamic-light getter and
vector-rotation helpers remain follow-ups. Benefit is inferred from the APIs and
their existing backing systems; this audit did not establish usage counts across
released mods.

## 1. Repair signed frame-query handles

QSS-M's [CL_Precache_Model](../../Quake/pr_cmds.c) already searches the server
precache before its client-only table. Client-only handles are negative, and
`PR_CSQC_GetModel` resolves them. The extension registry supplies `getmodelindex`
only to MenuQC, however. More seriously, `PF_frameforname`, `PF_frametoname`, and
`PF_frameduration` require `modelindex >= 0` before invoking the resolver.

Runtime reproduction: load the same player model under a unique client-only
filename, then use `setmodel` on a CSQC entity. The model handle is `-1`, and
`modelnameforindex(-1)` correctly returns `progs/parity-only.mdl`, but
`frametoname(-1, 0)` returns empty and `frameforname(-1, "axrun1")` returns `-1`.
The server-precached original returns `axrun1` for frame 0. A separate grouped
flame-model control returned duration approximately `0.6` for the server model
and `0` for its client-only copy.

This repair is small: replace the three nonnegative model-handle guards with a
bounded signed check such as `modelindex > -65536 && modelindex < 65536`, then
let the VM resolver decide validity. The comparisons reject NaN/infinities before
conversion. Keep the existing nonnegative checks for frame indices.

Check the frame trio against server and client-only alias models, including a
grouped animation for duration, and preserve SSQC/MenuQC behavior.

## 2. Expose CSQC getmodelindex

Add a CSQC lookup wrapper with FTE's optional `queryonly` argument. Reuse the
existing positive/server and negative/client handle namespaces. Query-only
misses must not allocate, load, or poison a precache slot; loading failures also
need deliberate handling so later lookups can recover.

Check server hits, client hits, repeated query-only misses followed by successful
loads, missing files, and precache capacity. This is the larger part of the model
work, separate from the frame-query guard repair.

FTE reference: [model precache and getmodelindex implementation](https://github.com/fte-team/fteqw/blob/f937b9d88f71fc4429db5fe56c6a98d922711b2e/engine/client/pr_csqc.c#L3187).
Local entry points: `Quake/pr_cmds.c:2028`, `Quake/pr_ext.c:2323`,
`Quake/pr_ext.c:9712`.

## 3. Fix custom entity-token reset

`PF_cs_getentitytoken` at `Quake/pr_ext.c:7658` ignores the argument contents:
every call with an argument resets to `cl.worldmodel->entities`. FTE uses an
empty string for map reset and a nonempty string as the new input stream.

Runtime reproduction: after `getentitytoken("PARITY_TOKEN custom")`, the next
call returns `{` from the map instead of `PARITY_TOKEN`.

Handle nonempty input, empty/map reset, end of input, and reset without a world
model. Copy nonempty input into owned storage that stays alive across calls;
free the previous copy on reset and the remaining copy when the VM shuts down.
Check escaped strings too: QSS-M and FTE use
different parsing paths, so argument handling alone does not establish complete
lexer parity. Both inspected implementations return no token on the reset call;
the QSS-M generated description incorrectly promises the first token immediately.

FTE reference: [PF_cs_getentitytoken](https://github.com/fte-team/fteqw/blob/f937b9d88f71fc4429db5fe56c6a98d922711b2e/engine/client/pr_csqc.c#L4301).

## 4. Complete string-buffer search

QSS-M already owns string buffers, loads/writes them, sorts them, and has
`wildcmp`. `bufstr_find` alone is commented out at `Quake/pr_ext.c:10047`.

Port FTE's exact, prefix, suffix, substring, and wildcard matching, plus optional
starting index and positive step. Match miss/error return `-1`; skip unset
buffer entries. Respect VM ownership. Register #537 in the supported modules
and emit correct declarations/constants from `pr_dumpplatform`.

Check sparse buffers, each match mode, start/step, invalid handles, no match,
and a handle belonging to a different VM.

FTE reference: [match modes and PF_bufstr_find](https://github.com/fte-team/fteqw/blob/f937b9d88f71fc4429db5fe56c6a98d922711b2e/engine/common/pr_bgcmd.c#L5628).

## 5. Add drawline

The declaration is already present but commented out at
`Quake/pr_ext.c:9833`. QSS-M has the color, clipping, virtual-coordinate, and
2D primitive machinery needed to implement it. Register the appropriate number
for each VM: CSQC #315 and MenuQC #466.

Match the argument ABI and use the same drawflag handling as the other 2D calls.
Check horizontal, vertical, diagonal, clipped, and degenerate lines at more than
one UI scale. The pinned FTE implementation ignores its width parameter; width
support in QSS-M would be an additional feature rather than a parity requirement.

Include a server-approved full-CSQC case as well as the simple HUD and MenuQC
paths. The commented registry row uses `PF_FullCSQCOnly`, but that macro expands
to `NULL` at `Quake/pr_ext.c:9638`; it is an unimplemented-handler placeholder,
not a permission gate. Once a real handler is registered, any full-only policy
would need an explicit check. A pure 2D line does not need game-state access, so
do not infer a restriction from the placeholder name.

FTE reference: [PF_CL_drawline](https://github.com/fte-team/fteqw/blob/f937b9d88f71fc4429db5fe56c6a98d922711b2e/engine/client/pr_menu.c#L1063).

## 6. Honor additive drawing flags

In [pr_ext.c](../../Quake/pr_ext.c), the flags reads are commented out in
`PF_cl_drawcharacter`, `PF_cl_drawrawstring`, `PF_cl_drawstring`,
`PF_cl_drawpic`, `PF_cl_drawsubpic`, and `PF_cl_drawfill`. The calls exist but
always use the surrounding normal alpha-blending state.

Runtime pixel reproduction: draw half-alpha red over opaque blue twice, once
with flag 0 and once with flag 1. Both samples are RGB `(128, 0, 127)`.
Additive drawing should preserve the blue destination component; the two
results should differ.

Port scope:

- Implement the flag-1 additive behavior shared by FTE's drawing functions.
- Apply it consistently to the existing functions with a drawflag argument.
- Change blending outside `glBegin`/`glEnd`, and finish the additive primitive
  before restoring normal blending. Follow the existing additive-pass pattern
  at `Quake/gl_draw.c:2184` / `2348`.
- Restore normal blend state after each call; respect clipping and texture state.
- Check omitted flags after a call with flags, additive pixels, and a subsequent
  normal draw so stale optional arguments or GL state cannot hide a defect.

This is the highest-risk item in today's set because state leakage can affect
unrelated HUD draws. The inspected CSQC paths close their immediate-mode
primitives inside each call: strings batch glyphs within that call, while
`Draw_SubPic` and `Draw_PicPolygon` also begin/end their own primitives. There is
no shared deferred CSQC batch to flush in this checkout. If these paths acquire
buffering during implementation, flush queued geometry before each blend-state
transition; do not add `glFlush` as a substitute for correct primitive ordering.

The pinned FTE selector masks with `flag & 3` and implements additive for 1;
it returns ordinary behavior for 2 and 3. Do not describe modulation modes 2/3
as required FTE behavior without a different reference implementation.

FTE reference: [PF_SelectDPDrawFlag and drawfill](https://github.com/fte-team/fteqw/blob/f937b9d88f71fc4429db5fe56c6a98d922711b2e/engine/client/pr_menu.c#L22).
Local entry points: `Quake/pr_ext.c:6033`, `Quake/pr_ext.c:6177`,
`Quake/pr_ext.c:6264`, `Quake/pr_ext.c:6282`.

## Follow-up: close the ordinary dynamic-light API

`PF_cs_addlight` returns a handle, and `PF_cs_dynamiclight_set` can change
origin, color, radius, style, die time, RGB decay, and radius decay. The getter
entry is commented out at `Quake/pr_ext.c:9905`.

Add #372 for the ordinary fields QSS-M actually stores. Define deterministic
returns for unsupported fields and invalid handles, clearing the full vector
return when appropriate. Consider FTE's field `-1` capacity query explicitly.
Do not advertise projected-light, cubemap, or real-time-light capabilities.
Check add/get and set/get round trips for every supported scalar/vector field.

FTE reference: [PF_R_DynamicLight_Get](https://github.com/fte-team/fteqw/blob/f937b9d88f71fc4429db5fe56c6a98d922711b2e/engine/client/pr_csqc.c#L1208).
Local backing implementation: `Quake/pr_ext.c:7972`, `Quake/pr_ext.c:8027`.

## Follow-up: add the two basis-rotation helpers

QSS-M already has `R_ConcatRotations` and vector math. The two registry entries
at `Quake/pr_ext.c:9751` are commented out. Port the transformations and their
updates to `v_forward`, `v_right`, and `v_up`.

Rewrite the `rotatevectorsbyvectors` row when registering it: the commented row
has `PF_NoMenu, 236` in the wrong order. The documented builtin number must
precede the menu-handler fields, as in `..., 236, PF_NoMenu, ...`; merely
uncommenting it will not produce a valid registration.

Keep FTE's matrix multiplication order, right-vector negation, and model-pitch
convention. Identity-only checks will miss those errors: test 90-degree pitch,
yaw and roll, composition on an already rotated basis, and a non-orthonormal
basis for the matrix variant. The functions can benefit SSQC as well as CSQC.

FTE reference: [rotation helpers](https://github.com/fte-team/fteqw/blob/f937b9d88f71fc4429db5fe56c6a98d922711b2e/engine/common/pr_bgcmd.c#L6860).

## Other gaps and exclusions

- **`findfont` #356 is a small real gap**, with a backing bitmap-slot table
  already present. It is a good follow-up for a target HUD that needs it.
  Resolve the handle convention before copying: pinned FTE `findfont` returns
  named slot + 1, while `loadfont` returns the slot itself and drawing indexes
  the slot directly. A new QSS-M implementation should have an explicit,
  tested policy for that upstream inconsistency. Adding it alone also does
  not supply FTE's TrueType support or justify advertising all of DP_GFX_FONTS.
  [FTE font lookup and loading](https://github.com/fte-team/fteqw/blob/f937b9d88f71fc4429db5fe56c6a98d922711b2e/engine/client/pr_menu.c#L242).
- **CSQC `getmousepos` #344 is absent**, but it must return absolute coordinates
  in cursor mode and accumulated deltas otherwise, consuming deltas as FTE
  does. QSS-M currently sends mouse events directly to QC. This needs deliberate
  integration with event consumption, HID/SDL sources, focus, and UI scaling;
  copying the MenuQC position getter would not implement it.
- **`drawtextfield`** would be useful, but wrapping, alignment, markup, and font
  measurement make it a larger text-layout change than `drawline`.
- **`resourcestatus` / `freepic`** need truthful state and ownership across
  caches. Avoid a success stub or a free operation that invalidates shared assets.
- **Hash tables / JSON** can be worthwhile for data-heavy menus, but require
  owned object lifetimes, value/string handling, and VM teardown work. No usage
  evidence here puts them ahead of the core drawing/model gaps.
- **Skeletal objects, tags, 3D polygons, full shader semantics, render targets,
  and reverb** are larger subsystem additions. Existing 2D shader-image aliases
  and IQM/MD3 loading do not make those interfaces complete.
- **MDL single-frame names are already copied** by `Mod_LoadAliasFrame` at
  `Quake/gl_model.c:5632`. The harness README's claim that the MDL loader never
  copies names is stale. Group names are a separate issue; do not present all
  MDL naming as a missing port.
- **Full view hooks, rotated pictures, bitmap `loadfont`, shader image aliases,
  `dynamiclight_set`, and basic scene rendering already exist.** They were
  excluded from the missing-feature shortlist.

## Verification performed

The isolated probe was compiled to version-6 QC with the local FTEQCC and run
against `Quake/quakespasm` using SDL's offscreen video driver and software GL.
The engine exited normally. Logs contain expected offscreen mouse-mode warnings.
No FTE binary comparison or cross-platform execution was performed.

Artifacts remain in `/tmp/qssm-csqc-parity-audit/`:

- `inputs.json`: QSS/FTE source IDs, tested binary SHA-256, engine arguments.
- `parity/src/probe.qc`: the QC reproduction, using generated engine declarations.
- `engine-final.log`: seven named builtins return unavailable; client-only model
  frame-name failure; server-model control; ignored custom token stream.
- `engine-control.log`: the grouped flame-model duration control.
- `pixel-summary.json`: identical normal/additive RGB samples.
- `parity/screenshots/`: TGA captures of the blend probe.

The named-builtin probe returned 0 for `getmodelindex`, `findfont`, `drawline`,
`bufstr_find`, `dynamiclight_get`, `rotatevectorsbyangle`, and
`rotatevectorsbyvectors`. Source inspection distinguished these actual gaps
from core builtins, aliases, commented rows, and FTE conditional features.

For implementation, extend `Misc/csqcharness` with the targeted semantic/pixel
checks above and regenerate both CSQC/MenuQC headers. Use a full-CSQC test case
where a capability is restricted to server-approved CSQC; the existing harness
primarily exercises the simple HUD entry point.
