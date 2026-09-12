"""Exercise production completion policy, dictionary, Tab and submit branches.

Run with python3 Misc/stress/test_console_completion.py (requires a C compiler).
Registry and engine side effects are stubbed; no game or user config is opened.
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
#include <string.h>
#include <strings.h>
typedef enum { false, true } qboolean;
#define MAXCMDLINE 256
#define CMDLINES 64
#define MAX_CHAT_SIZE 128
#define MAX_CHAT_SIZE_EX 256
#define CVAR_NONE 0
#define q_min(a,b) ((a) < (b) ? (a) : (b))
#define q_strncasecmp strncasecmp
#define Q_strncpy strncpy
enum { ca_disconnected, ca_connected, GAME_DEATHMATCH, src_command, src_server };
enum { K_ENTER, K_ABUTTON, K_KP_ENTER, K_TAB };
typedef enum { TABCOMPLETE_AUTOHINT, TABCOMPLETE_USER } tabcomplete_t;
typedef struct cvar_s { const char *name; struct cvar_s *next; float value; } cvar_t;
typedef struct cmd_s { const char *name; int srctype; struct cmd_s *next; } cmd_function_t;
typedef struct alias_s { const char *name; struct alias_s *next; } cmdalias_t;
static cvar_t vars[] = {{"sensitivity", &vars[1]}, {"scr_conwidth", &vars[2]},
    {"host_maxfps", &vars[3]}, {"r_lerpmodels2"}};
static cmd_function_t commands[] = {{"connect", src_command, &commands[1]},
    {"say", src_command, &commands[2]}, {"__hidden", src_command, &commands[3]},
    {"serveronly", src_server}};
static cmdalias_t aliases[] = {{"myalias"}};
static cmd_function_t *cmd_functions = commands;
static cmdalias_t *cmd_alias = aliases;
static cvar_t cl_chatmode = {.value = 1}, con_autohint = {.value = 1},
    cl_chat_autocomplete = {.value = 1};
static struct { int state; } cls = {ca_connected};
static struct { int gametype, modtype; } cl;
static char key_lines[CMDLINES][MAXCMDLINE], key_tabhint[MAXCMDLINE], key_tabpartial[MAXCMDLINE];
static int key_linepos, edit_line, history_line, command_completions;
static char submitted[1024];
static size_t q_strlcpy(char *out, const char *in, size_t size) {
    size_t len = strlen(in);
    if (size) { size_t n = q_min(len, size - 1); memcpy(out, in, n); out[n] = 0; }
    return len;
}
static cvar_t *Cvar_FindVarAfter(const char *name, unsigned int flags) { return vars; }
static cvar_t *Cvar_FindVar(const char *name) {
    for (cvar_t *v = vars; v; v = v->next) if (!strcmp(v->name, name)) return v;
    return NULL;
}
static qboolean Cmd_Exists2(const char *name) {
    for (cmd_function_t *c = cmd_functions; c; c = c->next)
        if (!strcmp(c->name, name)) return true;
    return false;
}
static qboolean Cmd_AliasExists(const char *name) {
    for (cmdalias_t *a = cmd_alias; a; a = a->next)
        if (!strcmp(a->name, name)) return true;
    return false;
}
static void Cbuf_AddText(const char *text) { strcat(submitted, text); }
static void Con_Printf(const char *fmt, ...) {}
static qboolean History_SaveHistoryEnabled(void) { return false; }
static qboolean Key_ConsoleQuitMistype(const char *text) { return false; }
static void SCR_UpdateScreen(void) {}
static void Char_Console2(int ch) {
    key_lines[edit_line][key_linepos++] = ch;
    key_lines[edit_line][key_linepos] = 0;
}
'''
source += function((ROOT / "Quake/cmd.c").read_text(), "qboolean Cmd_IsReservedName (")
source += function(console, "qboolean Con_TokenIsCommandPrefix (")
for declaration in ("qboolean CheckForCommand(", "int Key_ConsoleInputLimit (",
                    "qboolean Key_ConsoleLineIsChat (",
                    "qboolean Key_ConsoleLineIsCommandInProgress ("):
    source += function(keys, declaration)
source += keys[keys.index("#define CHAT_AUTOCOMPLETE_MIN_PREFIX"):
               keys.index("static unsigned int chat_edit_generation")]
source += keys[keys.index("static char con_autocomplete_suffix"):
               keys.index("static void Chat_HistoryResetBrowse")]
source += keys[keys.index("static qboolean ChatAuto_IsAsciiLetter"):
               keys.index("static void ChatAuto_Refresh")]
for declaration in ("static void ConAuto_Refresh (", "const char *Key_GetConsoleAutocompleteSuffix (",
                    "qboolean Key_ConsoleAcceptAutocomplete (", "static void Key_ConsoleCommitTabHint ("):
    source += function(keys, declaration)

# Execute the production autohint gates; record reaching the completion engine.
gate = console[console.index("\tkey_tabhint[0] = '\\0';", console.index("void Con_TabComplete (")):]
source += "void Con_TabComplete(tabcomplete_t mode) {\n"
source += gate[:gate.index("\n\t// if editline is empty")]
source += "++command_completions;\n}\n"

# Keep the actual submit/Tab switch branches, stubbing history and output above.
dispatch = function(keys, "void Key_Console (int key)")
source += dispatch[:dispatch.index("\n\tcase K_BACKSPACE:")] + "\n\t}\n}\n"
source += r'''
static void line(const char *text) {
    key_lines[edit_line][0] = ']';
    q_strlcpy(key_lines[edit_line] + 1, text, MAXCMDLINE - 1);
    key_linepos = strlen(key_lines[edit_line]);
    key_tabpartial[0] = key_tabhint[0] = 0;
}
static void command(const char *text) {
    line(text);
    assert(Key_ConsoleLineIsCommandInProgress());
    assert(!*Key_GetConsoleAutocompleteSuffix());
    int before = command_completions;
    Con_TabComplete(TABCOMPLETE_AUTOHINT);
    assert(command_completions == before + 1);
    Key_Console(K_TAB);
    assert(command_completions == before + 2);
    assert(!strcmp(key_lines[edit_line] + 1, text));
}
int main(void) {
    assert(!Con_TokenIsCommandPrefix(NULL));
    assert(!Con_TokenIsCommandPrefix(""));
    const char *prefixes[] = {"sensi", "scr_con", "host_max", "r_lerpmodels2",
        "conn", "CONN", "SeNsi", "myal", "MYAL", "sensitivity"};
    for (size_t i = 0; i < sizeof(prefixes)/sizeof(*prefixes); ++i) command(prefixes[i]);
    const char *noncommands[] = {"", " sensi", "  conn", "\tconn", "hi conn",
        "conn ", "conn;", "conn\"", "__hid", "servero", "ensitivity", "connections"};
    for (size_t i = 0; i < sizeof(noncommands)/sizeof(*noncommands); ++i) {
        line(noncommands[i]); assert(!Key_ConsoleLineIsCommandInProgress());
    }
    line("connect"); key_linepos = 4;
    assert(!Key_ConsoleLineIsCommandInProgress());
    assert(!*Key_GetConsoleAutocompleteSuffix());
    key_linepos = MAXCMDLINE; assert(!Key_ConsoleLineIsCommandInProgress());
    key_linepos = -1; assert(!Key_ConsoleLineIsCommandInProgress());

    // Real dictionary fixture, cached ghost clearing, and crossing back again.
    line("con"); char expected[MAXCMDLINE];
    ChatAuto_Suggest(key_lines[edit_line], key_linepos, 200, expected, sizeof(expected));
    assert(*expected); // this prefix really triggered the original collision
    line(" con"); assert(!strcmp(Key_GetConsoleAutocompleteSuffix(), expected));
    command("con");
    line(" con"); assert(!strcmp(Key_GetConsoleAutocompleteSuffix(), expected));
    int before = command_completions;
    Con_TabComplete(TABCOMPLETE_AUTOHINT); assert(command_completions == before);
    Key_Console(K_TAB); assert(command_completions == before);
    assert(strstr(key_lines[edit_line], expected));
    line("hi con"); assert(!strcmp(Key_GetConsoleAutocompleteSuffix(), expected));
    line("say con"); assert(!*Key_GetConsoleAutocompleteSuffix());
    line(" CON"); assert(*Key_GetConsoleAutocompleteSuffix());
    command("CON");

    // A registry change without a text edit must be rechecked when Tab accepts.
    cmd_functions = commands + 1; // temporarily remove connect
    line("con"); assert(*Key_GetConsoleAutocompleteSuffix());
    cmd_functions = commands;
    before = command_completions;
    Key_Console(K_TAB);
    assert(command_completions == before + 1);
    assert(!strcmp(key_lines[edit_line] + 1, "con"));

    // Backspacing through the command/chat boundary recomputes ownership.
    line("connections");
    while (key_linepos > 1) {
        key_lines[edit_line][--key_linepos] = 0;
        const char *suffix = Key_GetConsoleAutocompleteSuffix();
        if (Key_ConsoleLineIsCommandInProgress()) assert(!*suffix);
    }
    for (int mode = 0; mode <= 3; ++mode) {
        cl_chatmode.value = mode;
        line(" con");
        assert(!!*Key_GetConsoleAutocompleteSuffix() == (mode != 0));
        command("sensi");
        // All three submit keys must preserve the same typed text in chat.
        const int submit_keys[] = {K_ENTER, K_KP_ENTER, K_ABUTTON};
        for (size_t i = 0; i < sizeof(submit_keys)/sizeof(*submit_keys); ++i) {
            line("sensi"); strcpy(key_tabhint, "tivity"); submitted[0] = 0;
            Key_Console(submit_keys[i]);
            assert(!strcmp(submitted, mode == 1 || mode == 2 ? "say sensi\n" : "sensitivity\n"));
            line(" sensi"); strcpy(key_tabhint, "tive"); submitted[0] = 0;
            Key_Console(submit_keys[i]);
            assert(!strcmp(submitted, mode ? "say  sensi\n" : " sensitive\n"));
            line("sensitivity"); submitted[0] = 0;
            Key_Console(submit_keys[i]); assert(!strcmp(submitted, "sensitivity\n"));
        }
    }
    cl_chatmode.value = 1;
    line(" con"); assert(*Key_GetConsoleAutocompleteSuffix());
    con_autohint.value = 0; assert(!*Key_GetConsoleAutocompleteSuffix());
    con_autohint.value = 1; assert(*Key_GetConsoleAutocompleteSuffix());
    cl_chat_autocomplete.value = 0; assert(!*Key_GetConsoleAutocompleteSuffix());
    cl_chat_autocomplete.value = 1; assert(*Key_GetConsoleAutocompleteSuffix());
    cls.state = ca_disconnected; assert(!*Key_GetConsoleAutocompleteSuffix());
    puts("Console completion: prefix ownership, dictionary cache, Tab and all submit keys passed");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-console-completion-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(source)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c99", "-g", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-missing-field-initializers",
        "-Wno-sign-compare",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-I", str(ROOT / "Quake"), str(path / "test.c"), "-o", str(path / "test"),
    ], check=True)
    subprocess.run([str(path / "test")], check=True)
