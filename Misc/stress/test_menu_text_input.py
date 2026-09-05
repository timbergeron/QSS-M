"""Compile the production input-policy functions with SDL/QC test doubles.

No game assets or window are needed. Checks cover IME requests and legacy settings,
VM reentry, legacy typing, and focus changes during event dispatch.
"""

from pathlib import Path
import os
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
keys = (ROOT / "Quake/keys.c").read_text()
input_source = (ROOT / "Quake/in_sdl.c").read_text()
menu_text_default = re.search(
    r'static cvar_t in_menutextinput = \{"in_menutextinput", "([^"]+)"', keys
).group(1)


def function(source, declaration):
    start = source.index(declaration)
    # These functions have their closing brace alone at column zero.
    end = source.index("\n}", start) + 2
    return source[start:end]


dispatch = keys.split("\t//Spike -- give menuqc a chance to handle (and swallow) key events.\n", 1)[1]
dispatch = dispatch.split("\t//Spike -- give csqc a chance", 1)[0]

source = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef enum { false, true } qboolean;
enum { key_game, key_console, key_message, key_menu } key_dest;
enum { K_SHIFT, K_CTRL, K_COMMAND, K_ALT };
enum { KMOD_CAPS = 1, KMOD_NUM = 2 };
qboolean keydown[4], native_text, requested_text;
struct { qboolean active; } key_inputgrab;
typedef struct {
    struct { int m_draw, Menu_TextEntry, Menu_InputEvent, m_keydown; } extfuncs;
} qcvm_t;
struct { qcvm_t menu_qcvm; } cls;
qcvm_t *qcvm;
float return_value;
#define OFS_RETURN 0
#define G_FLOAT(x) return_value
int query_count, starts, stops, mods;
qboolean sdl_active, textmode, consume_key, leave_menu, reenter_query;
struct { float value; } in_debugkeys, in_menutextinput = {MENU_TEXT_DEFAULT};
struct { int key, ch; qboolean down; } events[8];
int event_count, dispatch_fallthrough;
qboolean Key_TextEntry(void);
int SDL_GetModState(void) { return mods; }
qboolean SDL_IsTextInputActive(void) { return sdl_active; }
void SDL_StartTextInput(void) { sdl_active = true; ++starts; }
void SDL_StopTextInput(void) { sdl_active = false; ++stops; }
int SDL_EnableUNICODE(int enabled) {
    int old = sdl_active;
    if (enabled >= 0 && enabled != old) {
        if (enabled) SDL_StartTextInput(); else SDL_StopTextInput();
    }
    return old;
}
qboolean Key_IsShortcutModifierDown(void) { return keydown[K_CTRL] || keydown[K_COMMAND]; }
qboolean M_TextEntry(void) { return native_text; }
void PR_SwitchQCVM(qcvm_t *vm) { assert(!qcvm || !vm); qcvm = vm; }
void PR_ExecuteProgram(int fn) {
    assert(qcvm == &cls.menu_qcvm && fn == 7);
    ++query_count;
    if (reenter_query) {
        assert(!Key_TextEntry());
        assert(qcvm == &cls.menu_qcvm);
    }
    return_value = requested_text;
}
void Con_Printf(const char *fmt, ...) { (void)fmt; }
double Sys_DoubleTime(void) { return 0; }
qboolean Menu_HandleKeyEvent(qboolean down, int key, int ch) {
    assert(event_count < 8);
    events[event_count].key = key;
    events[event_count].ch = ch;
    events[event_count++].down = down;
    if (leave_menu && down && key) key_dest = key_console;
    return consume_key;
}
'''
source = f"#define MENU_TEXT_DEFAULT {float(menu_text_default)}\n" + source
source += "\n".join(re.findall(r"^#define\s+K_KP_\w+\s+\d+.*$",
                                   (ROOT / "Quake/keys.h").read_text(), re.MULTILINE)) + "\n"
source += function(keys, "static int Key_MenuChar (int key, int keycode)") + "\n"
source += function(keys, "qboolean Key_TextEntry (void)") + "\n"
source += function(input_source, "void IN_UpdateInputMode (void)") + "\n"
source += "void dispatch_key(int key, qboolean down, int keycode) {\n" + dispatch
source += "++dispatch_fallthrough;\n}\n"
source += r'''
int main(void) {
    assert(in_menutextinput.value == 0);
    key_dest = key_menu;
    native_text = true;  // stale native text-field state must not enable a QC menu
    cls.menu_qcvm.extfuncs.m_draw = 1;
    cls.menu_qcvm.extfuncs.Menu_InputEvent = 2;
    cls.menu_qcvm.extfuncs.m_keydown = 3;
    assert(!Key_TextEntry());
    cls.menu_qcvm.extfuncs.Menu_TextEntry = 7;
    assert(!Key_TextEntry() && !qcvm);
    requested_text = true;
    assert(Key_TextEntry() && !qcvm);
    IN_UpdateInputMode();
    assert(sdl_active && starts == 1);
    IN_UpdateInputMode();
    assert(starts == 1);
    assert(Key_MenuChar(0, 'a') == 0); // SDL supplies the character exactly once
    requested_text = false;
    IN_UpdateInputMode();
    assert(!sdl_active && stops == 1);
    assert(Key_MenuChar(0, 'z') == 'z'); // layout keycode, independent of scan position
    assert(Key_MenuChar(0, 0) == 0 && Key_MenuChar(0, 128) == 0);
    keydown[K_SHIFT] = true;
    assert(Key_MenuChar(0, 'z') == 'Z' && Key_MenuChar(0, '6') == '^');
    mods = KMOD_CAPS;
    assert(Key_MenuChar(0, 'z') == 'z');
    keydown[K_SHIFT] = false;
    assert(Key_MenuChar(0, 'z') == 'Z');
    const int keypad[] = {K_KP_INS, K_KP_END, K_KP_DOWNARROW, K_KP_PGDN,
        K_KP_LEFTARROW, K_KP_5, K_KP_RIGHTARROW, K_KP_HOME, K_KP_UPARROW, K_KP_PGUP};
    mods = KMOD_NUM;
    for (int digit = 0; digit < 10; ++digit)
        assert(Key_MenuChar(keypad[digit], 0x40000062) == '0' + digit);
    assert(Key_MenuChar(K_KP_DEL, 0x40000063) == '.');
    assert(Key_MenuChar(K_KP_ENTER, 0x40000058) == 0);
    assert(Key_MenuChar(K_KP_END, 0) == 0);
    keydown[K_SHIFT] = true;
    assert(Key_MenuChar(K_KP_PLUS, 0x40000057) == '+');
    assert(Key_MenuChar(K_KP_MINUS, 0x40000056) == '-');
    assert(Key_MenuChar(K_KP_STAR, 0x40000055) == '*');
    assert(Key_MenuChar(K_KP_SLASH, 0x40000054) == '/');
#if defined(PLATFORM_OSX)
    assert(Key_MenuChar(K_KP_END, 0x40000059) == '1');
#else
    assert(Key_MenuChar(K_KP_END, 0x40000059) == 0);
#endif
    keydown[K_SHIFT] = false;
    mods = 0;
#if defined(PLATFORM_OSX)
    assert(Key_MenuChar(K_KP_END, 0x40000059) == '1');
#else
    assert(Key_MenuChar(K_KP_END, 0x40000059) == 0);
#endif
    for (int k = K_CTRL; k <= K_ALT; ++k) {
        keydown[k] = true;
        assert(Key_MenuChar(0, 'v') == 0);
        keydown[k] = false;
    }
    consume_key = true;
    dispatch_key('a', true, 'a');
    assert(event_count == 2);
    assert(events[0].key == 'a' && events[0].ch == 0);
    assert(events[1].key == 0 && events[1].ch == 'a');
    mods = KMOD_NUM;
    event_count = 0;
    dispatch_key(K_KP_END, true, 0x40000059);
    assert(event_count == 2 && events[0].key == K_KP_END);
    assert(events[1].key == 0 && events[1].ch == '1');
    mods = 0;
    event_count = 0;
    dispatch_key('a', false, 'a');
    assert(event_count == 1 && !events[0].down && events[0].ch == 0);
    event_count = 0;
    dispatch_key(128, true, 0x40000052); // up arrow
    assert(event_count == 1 && events[0].key == 128 && !events[0].ch);
    leave_menu = true;
    consume_key = false;
    event_count = 0;
    dispatch_key('`', true, '^');
    assert(key_dest == key_console && event_count == 1);
    assert(dispatch_fallthrough == 0); // closing a menu cannot trigger a game binding
    IN_UpdateInputMode();
    assert(sdl_active && starts == 2);
    key_dest = key_game;
    IN_UpdateInputMode();
    assert(!sdl_active && stops == 2);
    key_dest = key_message;
    assert(Key_TextEntry());
    key_inputgrab.active = true;
    int old_queries = query_count;
    key_dest = key_menu;
    assert(!Key_TextEntry() && query_count == old_queries);
    key_inputgrab.active = false;

    // Polling from any active VM must leave it intact, without executing QC.
    qcvm_t other_vm = {0};
    requested_text = true;
    qcvm = &other_vm;
    assert(!Key_TextEntry() && query_count == old_queries && qcvm == &other_vm);
    qcvm = &cls.menu_qcvm;
    assert(!Key_TextEntry() && query_count == old_queries && qcvm == &cls.menu_qcvm);
    qcvm = NULL;
    reenter_query = true;
    assert(Key_TextEntry() && query_count == old_queries + 1 && !qcvm);
    reenter_query = false;

    // Rows: callback absent/present, unfocused/focused. Columns: old -1, 0, 1.
    const qboolean expected[2][2][3] = {
        {{false, false, true}, {false, false, true}},
        {{false, false, false}, {true, true, true}}
    };
    for (int modern = 0; modern <= 1; ++modern) {
        cls.menu_qcvm.extfuncs.Menu_TextEntry = modern ? 7 : 0;
        for (int focus = 0; focus <= 1; ++focus) {
            requested_text = focus;
            for (int setting = -1; setting <= 1; ++setting) {
                in_menutextinput.value = setting;
                old_queries = query_count;
                if (modern) {
                    qcvm = &other_vm;
                    assert(!Key_TextEntry() && qcvm == &other_vm);
                    qcvm = NULL;
                    assert(query_count == old_queries);
                }
                IN_UpdateInputMode();
                qboolean wants_text = expected[modern][focus][setting + 1];
                assert(sdl_active == wants_text && Key_TextEntry() == wants_text);
                assert(query_count == old_queries + (modern ? 2 : 0));
                assert(Key_MenuChar(0, 'a') == (wants_text ? 0 : 'a'));
                assert(Key_MenuChar(K_KP_PLUS, 0x40000057) == (wants_text ? 0 : '+'));
            }
        }
        in_menutextinput.value = 1;
        key_inputgrab.active = true;
        assert(!Key_TextEntry());
        key_inputgrab.active = false;
        key_dest = key_game;
        assert(!Key_TextEntry());
        key_dest = key_menu;
    }

    // A saved legacy setting cannot force the IME onto the next mod's navigation.
    cls.menu_qcvm.extfuncs.Menu_TextEntry = 0;
    in_menutextinput.value = 1;
    IN_UpdateInputMode();
    assert(sdl_active);
    cls.menu_qcvm.extfuncs.Menu_TextEntry = 7;
    requested_text = false;
    IN_UpdateInputMode();
    assert(!sdl_active);
    requested_text = true;
    in_menutextinput.value = 0;
    IN_UpdateInputMode();
    assert(sdl_active); // default legacy policy cannot silence a focused field

    in_menutextinput.value = 0;
    key_dest = key_console;
    assert(Key_TextEntry());
    key_dest = key_message;
    assert(Key_TextEntry());
    key_dest = key_menu;
    memset(&cls, 0, sizeof(cls)); // unloading the QC menu restores native policy
    assert(Key_TextEntry());
    native_text = false;
    in_menutextinput.value = 1;
    assert(!Key_TextEntry());
    puts("PASS: menu IME policy, callback precedence, VM reentry, keyboard fallback, and focus transitions");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-menu-input-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(source)
    for defines in (["-DUSE_SDL2=1"], [], ["-DUSE_SDL2=1", "-DPLATFORM_OSX=1"]):
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                        *defines, str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
