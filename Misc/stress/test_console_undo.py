"""Exercise production console editing and undo through keyboard/text events.

Run with python3 Misc/stress/test_console_undo.py (requires a C compiler).
Engine output, clipboard access, and completion candidates are stubbed; the
editor, undo buffer, shortcut modifier, and console toggle are production code.
"""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
keys = (ROOT / "Quake/keys.c").read_text()
console = (ROOT / "Quake/console.c").read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


source = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef enum { false, true } qboolean;
#define COMPILE_TIME_ASSERT(name, expr) _Static_assert(expr, #name)
#include "keys.h"
#define q_min(a,b) ((a) < (b) ? (a) : (b))
#define CLAMP(a,b,c) ((b) < (a) ? (a) : ((b) > (c) ? (c) : (b)))
#define SDL_assert assert
#define Q_strcpy strcpy
#define CIF_CHAT 1
#define CHAT_TIMER_DELAY 3.0
enum { ca_disconnected, ca_connected, GAME_DEATHMATCH };
typedef enum { TABCOMPLETE_AUTOHINT, TABCOMPLETE_USER } tabcomplete_t;
typedef struct { float value; } cvar_t;
static cvar_t cl_chatmode, con_clear_input_on_toggle;
static struct { int state; char userinfo[8]; } cls = {ca_connected};
static struct { int gametype, modtype; double expectingpingtimes; } cl;
static struct { int height; } vid = {480};
static struct { int active, lastchar; } key_inputgrab;
char key_lines[CMDLINES][MAXCMDLINE], key_tabhint[MAXCMDLINE];
static char key_tabpartial[MAXCMDLINE], history_saved_current[MAXCMDLINE];
size_t key_linepos;
int edit_line, key_insert = 1;
double key_blinktime;
keydest_t key_dest = key_console;
qboolean keydown[MAX_KEYS];
static qboolean consolekeys[MAX_KEYS], con_forcedup;
static int history_line, chat_setinfo_defer;
static int con_current, con_linewidth = 80, con_vislines = 240;
static int con_totallines = 64, con_backscroll, glheight = 480;
static char con_storage[80 * 64], *con_text = con_storage;
static double realtime, con_times[4];
static const char *clipboard, *completion, *chat_completion;
static char submitted[1024];
static int hints;
static qboolean save_history = true;
static size_t q_strlcpy(char *out, const char *in, size_t size) {
    size_t len = strlen(in);
    if (size) { size_t n = q_min(len, size - 1); memcpy(out, in, n); out[n] = 0; }
    return len;
}
static void Con_TabComplete(tabcomplete_t mode) {
    key_tabhint[0] = 0;
    if (mode == TABCOMPLETE_AUTOHINT) {
        key_tabpartial[0] = 0; ++hints;
    } else if (completion) {
        q_strlcpy(key_lines[edit_line] + 1, completion, MAXCMDLINE - 1);
        key_linepos = strlen(key_lines[edit_line]);
        strcpy(key_tabpartial, "partial");
    }
}
qboolean Key_ConsoleAcceptAutocomplete(void) {
    if (!chat_completion) return false;
    q_strlcpy(key_lines[edit_line] + key_linepos, chat_completion, MAXCMDLINE - key_linepos);
    key_linepos = strlen(key_lines[edit_line]);
    return true;
}
qboolean Key_ConsoleLineIsChat(void) { return cl_chatmode.value != 0; }
static char *PL_GetClipboardData(void) {
    if (!clipboard) return NULL;
    char *copy = malloc(strlen(clipboard) + 1); assert(copy);
    return strcpy(copy, clipboard);
}
static void Z_Free(void *p) { free(p); }
static void Cbuf_AddText(const char *text) { strcat(submitted, text); }
static void Con_Printf(const char *fmt, ...) {}
static qboolean History_SaveHistoryEnabled(void) { return save_history; }
static qboolean Key_ConsoleQuitMistype(const char *text) { return false; }
static void SCR_UpdateScreen(void) {}
static void Con_MoveSelection(int x, int y) {}
static void Con_SelectAll(void) {}
static void Con_Copy_f(void) {}
static void AdjustConsoleHeight(int amount) {}
static void SetChatInfo(int info) {}
static void Info_GetKey(const char *info, const char *key, char *out, size_t size) { out[0] = 0; }
static void Host_CancelDeferredCall(int handle) {}
static int Host_DeferCall(double delay, void (*callback)(void *), void *data) { return 1; }
static void ChatSetInfo_Deferred(void *data) {}
static void Char_Message(int key) {}
static void M_Charinput(int key) {}
static qboolean Menu_HandleKeyEvent(qboolean down, int key, int ch) { return false; }
static void M_ToggleMenu(int mode) {}
void Key_ReleaseMouseButtons(void) {}
static void Con_LeaveCursorMode(void) {}
static void Con_EnterCursorMode(void) {}
static void Con_SetHotLink(const char *link) {}
static void Con_ClearSelection(void) {}
static void SCR_EndLoadingPlaque(void) {}
static void IN_UpdateGrabs(void) {}
'''
for declaration in ("qboolean Key_IsShortcutModifierDown (", "int Key_ConsoleInputLimit (",
                    "static void PasteToConsole (", "void Char_Console2(",
                    "static qboolean Key_IsWordSeparator(", "static int Key_FindWordBoundary("):
    source += function(keys, declaration)
source += keys[keys.index("#define CONSOLE_UNDO_STATES"):
               keys.index("/*\n====================\nKey_ConsoleQuitMistype")]
for declaration in ("static void Key_ConsoleCommitTabHint (", "static void Key_ConsoleKey (",
                    "void Key_Console (", "void Char_Console(", "void Char_Event (",
                    "static void History_ClearMemory ("):
    source += function(keys, declaration)
source += function(console, "void Con_ToggleConsole_f (")
source += r'''
static void expect(const char *text, size_t cursor) {
    assert(!strcmp(key_lines[edit_line] + 1, text));
    assert(key_lines[edit_line][0] == ']');
    assert(key_linepos == cursor);
}
static void fresh(const char *text) {
    History_ClearMemory();
    strcpy(key_lines[edit_line] + 1, text); key_linepos = strlen(text) + 1;
    Key_ConsoleUndoReset();
    memset(keydown, 0, sizeof(keydown));
    for (int i = 0; i < MAX_KEYS; ++i) consolekeys[i] = true;
    key_dest = key_console; key_insert = 1;
    cl_chatmode.value = con_clear_input_on_toggle.value = 0;
    key_tabhint[0] = key_tabpartial[0] = submitted[0] = 0;
    clipboard = completion = chat_completion = NULL;
    save_history = true; realtime += 2;
}
static void type(const char *text) {
    for (; *text; ++text) { Key_Console(*text); Char_Event(*text); realtime += 0.01; }
}
static void shortcut(int key, qboolean shift) {
    keydown[K_CTRL] = true; keydown[K_SHIFT] = shift;
    Key_Console(key); Char_Event(key); // SDL text must not insert the shortcut.
    keydown[K_CTRL] = keydown[K_SHIFT] = false;
}
static void undo(void) { shortcut('z', false); }
static void redo(void) { shortcut('Z', true); }
int main(void) {
    fresh(""); undo(); redo(); expect("", 1);
    type("hello"); undo(); expect("", 1); redo(); expect("hello", 6);
    realtime += 2; type(" world"); undo(); expect("hello", 6);
    type("!"); redo(); expect("hello!", 7); undo(); expect("hello", 6);
    undo(); expect("", 1);

    fresh("abc"); type("d"); Key_Console(K_LEFTARROW); type("X");
    expect("abcXd", 5); undo(); expect("abcd", 4); redo(); expect("abcXd", 5);
    Key_Console(K_INS); type("Y"); expect("abcXY", 6);
    undo(); expect("abcXd", 5); redo(); expect("abcXY", 6);

    fresh("abcdef"); Key_Console(K_BACKSPACE); Key_Console(K_BACKSPACE);
    expect("abcd", 5); undo(); expect("abcdef", 7); redo(); expect("abcd", 5);
    Key_Console(K_HOME); Key_Console(K_DEL); Key_Console(K_DEL);
    expect("cd", 1); undo(); expect("abcd", 1);
    fresh("one two"); shortcut(K_BACKSPACE, false); expect("one ", 5);
    undo(); expect("one two", 8); Key_Console(K_HOME);
    shortcut(K_DEL, false); expect("two", 1); undo(); expect("one two", 1);

    fresh("draft"); shortcut('u', false); expect("", 1); undo(); expect("draft", 6);
    redo(); expect("", 1); undo(); shortcut('d', false); expect("", 1);
    undo(); expect("draft", 6);
    Key_Console(K_HOME); clipboard = "pasted\nignored"; shortcut('v', false);
    expect("pasteddraft", 7); undo(); expect("draft", 1); redo(); expect("pasteddraft", 7);
    undo(); keydown[K_SHIFT] = true; Key_Console(K_INS); keydown[K_SHIFT] = false;
    expect("pasteddraft", 7); undo(); clipboard = NULL; shortcut('v', false);
    redo(); expect("pasteddraft", 7); undo(); clipboard = "\n"; shortcut('v', false);
    redo(); expect("pasteddraft", 7);

    fresh("ma"); completion = "map "; Key_Console(K_TAB); expect("map ", 5);
    completion = "maps "; Key_Console(K_TAB); expect("maps ", 6);
    int before = hints; undo(); expect("map ", 5);
    assert(hints == before + 1 && !*key_tabhint && !*key_tabpartial);
    undo(); expect("ma", 3); redo(); expect("map ", 5);
    completion = NULL; Key_Console(K_TAB); redo(); expect("maps ", 6);
    fresh("hel"); chat_completion = "lo"; Key_Console(K_TAB); expect("hello", 6);
    undo(); expect("hel", 4); redo(); expect("hello", 6);

    fresh("draft"); strcpy(key_lines[CMDLINES - 1], "]old");
    Key_Console(K_UPARROW); expect("old", 4); Key_Console(K_DOWNARROW); expect("draft", 6);
    undo(); expect("old", 4); undo(); expect("draft", 6); redo(); expect("old", 4);
    Key_Console(K_UPARROW); Key_Console(K_DOWNARROW); expect("old", 4);
    type("!"); redo(); expect("old!", 5);
    fresh("same"); strcpy(key_lines[CMDLINES - 1], "]same");
    Key_Console(K_UPARROW); undo(); expect("same", 5);

    const int submits[] = {K_ENTER, K_KP_ENTER, K_ABUTTON};
    for (int i = 0; i < 3; ++i) for (int history = 0; history < 2; ++history) {
        fresh(""); save_history = history; type("echo x"); Key_Console(submits[i]);
        assert(!strcmp(submitted, "echo x\n")); undo(); expect("", 1);
        // Repeated commands reuse the current history slot.
        type("echo x"); Key_Console(submits[i]); undo(); expect("", 1);
    }
    fresh(""); cl_chatmode.value = 2; type("hi"); Key_Console(K_ENTER);
    undo(); expect(" ", 2); type("x"); undo(); expect(" ", 2);

    fresh(""); type("draft"); Con_ToggleConsole_f(); Con_ToggleConsole_f();
    undo(); expect("", 1); redo(); expect("draft", 6);
    con_clear_input_on_toggle.value = 1; Con_ToggleConsole_f(); Con_ToggleConsole_f();
    undo(); expect("", 1);
    fresh(""); type("draft"); History_ClearMemory(); undo(); expect("", 1);
    fresh(""); type("draft"); strcpy(key_lines[edit_line], "]external"); key_linepos = 9;
    undo(); expect("external", 9); type("!"); undo(); expect("external", 9);

    // More atomic edits than fit in the buffer, then walk both boundaries.
    fresh(""); clipboard = "x";
    for (int i = 0; i < 100; ++i) shortcut('v', false);
    for (int i = 0; i < 100; ++i) undo();
    assert(key_linepos == 38); // 100 edits minus the 63 retained undo steps.
    for (int i = 0; i < 100; ++i) redo();
    assert(key_linepos == 101);

    char full[MAXCMDLINE]; memset(full, 'x', sizeof(full) - 2); full[MAXCMDLINE - 2] = 0;
    fresh(""); type(full); expect(full, MAXCMDLINE - 1);
    type("y"); Char_Console2(' '); expect(full, MAXCMDLINE - 1);
    undo(); expect("", 1); redo(); expect(full, MAXCMDLINE - 1);
    clipboard = "y"; shortcut('v', false); expect(full, MAXCMDLINE - 1);
    Key_Console(K_HOME); clipboard = full; shortcut('v', false);
    expect(full, MAXCMDLINE - 1); // Identical replacement is a no-op.
    Key_Console(K_HOME); type("z"); assert(key_lines[edit_line][1] == 'z');
    undo(); expect(full, 1); redo(); assert(key_lines[edit_line][1] == 'z');

    fresh("abcd"); cl_chatmode.value = 1; cl.gametype = GAME_DEATHMATCH;
    for (int i = 0; i < 100; ++i) type("x");
    assert(key_linepos == MAX_CHAT_SIZE); undo(); expect("abcd", 5);

#if defined(PLATFORM_OSX)
    fresh(""); type("mac"); keydown[K_COMMAND] = true;
    Key_Console('z'); Char_Event('z'); expect("", 1);
    keydown[K_SHIFT] = true; Key_Console('Z'); Char_Event('Z'); expect("mac", 4);
    keydown[K_SHIFT] = false; clipboard = "!"; Key_Console('v'); Char_Event('v');
    expect("mac!", 5); Key_Console('z'); expect("mac", 4);
#endif
    puts("Console undo: typing, deletion, paste, completion, history, reset, bounds and shortcuts passed");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-console-undo-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(source)
    for platform in ([], ["-DPLATFORM_OSX"]):
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-Wno-sign-compare", "-Wno-missing-field-initializers",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(ROOT / "Quake"), *platform, str(path / "test.c"), "-o", str(path / "test"),
        ], check=True)
        subprocess.run([str(path / "test")], check=True)
