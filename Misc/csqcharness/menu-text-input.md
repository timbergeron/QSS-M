# MenuQC text input

By default, QSS-M keeps the IME and on-screen keyboard off while navigating a
scripted menu. Defining `Menu_InputEvent` or `m_keydown` does not request text
entry.

The archived `in_menutextinput` setting controls scripted menus that do not
export `Menu_TextEntry`:

| Value | Behavior |
| --- | --- |
| `0` (default) | Keep SDL text input off in legacy menus. |
| `1` | Keep SDL text input on while a legacy menu is open. |

Use `in_menutextinput 1` to restore layout-aware typing in older menus without
recompiling them. This can also activate an IME or on-screen keyboard during
navigation in those older menus, including the arrow-key interference this
change addresses. Return to `0` to disable this compatibility setting.
If a menu exports `Menu_TextEntry`, its focused-text request always wins in
both directions: the setting cannot force text input on or off for that menu.
Console, chat, native menus, and input-grab prompts keep their own policies.
Existing configs with the earlier experimental value `-1` behave like `0`.

A menu can opt in while a text field has focus by exporting this optional
callback:

```c
float() Menu_TextEntry =
{
    return text_field_has_focus;
};
```

Return zero when focus leaves the text field. The engine
queries this function before polling input each frame, only while the scripted
menu has focus and no QC VM is executing. It must only report state: do not
draw, execute commands, change focus, or unload the menu from the callback.
No builtin number is required. The declaration is also emitted by
`pr_dumpplatform -Tmenu`.

A focus change made while processing events takes effect on the next input
poll. A character already queued with the click or key that focuses a text
field can therefore still use the fallback.

While requested, character events come from SDL and use the user's keyboard
layout. QSS-M's existing printable-ASCII character limit still applies.

Without a request, key presses and releases still reach the menu. Printable
key presses also produce a separate character event (`scan = 0`) using the
SDL keycode, with Shift/Caps Lock for letters and a US punctuation fallback.
Keypad operators also produce characters. Keypad digits and the decimal point
type with Num Lock on and Shift released; on macOS they type without Num Lock.
Ctrl, Command, and Alt combinations do not generate fallback characters.
Menus requiring AltGr, dead-key composition, or layout-specific shifted
punctuation should implement `Menu_TextEntry` for their text fields, or users
can select `in_menutextinput 1` for legacy menus without the callback.

This fallback is disabled when SDL text input is active, preventing duplicate
characters. A key that immediately changes focus away from the menu does not
send its fallback character into the new destination. That key press is also
consumed, even if the menu handler returns zero, so closing a menu does not
execute the same key's gameplay binding.

The policy follows FTEQW's distinction between menu key handlers and explicit
on-screen-keyboard requests. FTE's `Menu_WantOSK` returns the active native
menu's boolean `showosk`, or -1 for no preference; the SDL backend resolves -1
through `sys_osk`, whose default is zero. An explicit native menu request
takes precedence over that setting. FTE updates `showosk` during menu drawing
according to whether the selected item is a text field; it is not limited to
menu construction. `Menu_TextEntry` is a proposed QSS-M
callback; the name has not been agreed with FTEQW upstream:

- https://github.com/fte-team/fteqw/blob/master/engine/client/menu.c
- https://github.com/fte-team/fteqw/blob/master/engine/client/in_sdl.c
- https://github.com/fte-team/fteqw/blob/master/engine/client/m_items.c

Run the focused input-policy regression checks with:

```sh
python3 Misc/stress/test_menu_text_input.py
```
