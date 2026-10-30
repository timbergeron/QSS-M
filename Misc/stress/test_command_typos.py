"""Exercise production unknown-name suggestions and command dispatch.

Run with python3 Misc/stress/test_command_typos.py (requires a C compiler).
Registry entries and engine side effects are stubbed; no game data is needed.
"""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
cmd = (ROOT / "Quake/cmd.c").read_text()
common = (ROOT / "Quake/common.c").read_text()


def function(declaration, text=cmd):
    start = text.index(declaration)
    return text[start:text.index("\n}", start) + 2] + "\n"


source = r'''
#define _GNU_SOURCE
#include <assert.h>
#include "q_ctype.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef enum { false, true } qboolean;
#include "cmd.h"
#define MAXCMDLINE 256
#define q_min(a,b) ((a) < (b) ? (a) : (b))
#define q_strcasecmp strcasecmp
#define q_strncasecmp strncasecmp
#define q_strcasestr strcasestr
typedef struct cvar_s {
    const char *name, *string;
    float value;
    struct cvar_s *next;
} cvar_t;
typedef struct cmdalias_s {
    struct cmdalias_s *next;
    char name[32];
    char *value;
} cmdalias_t;
static cmd_function_t *cmd_functions;
static cmdalias_t *cmd_alias;
static cvar_t *cvars, cmd_warncmd = {.value = 1}, developer;
static qboolean in_cfg_exec;
cmd_source_t cmd_source;
static char output[65536], inserted[1024], token[4096];
static char *cmd_argv[] = {token};
static int argc, executed, cvar_reads, vm_calls;
typedef struct { struct { int CSQC_ConsoleCommand, m_consolecommand; } extfuncs; } qcvm_t;
static qcvm_t *qcvm;
static struct { qcvm_t qcvm; } cl;
static struct { qcvm_t menu_qcvm; } cls;
static struct { const char *name; } client = {"test"}, *host_client = &client;
static int vm_arg;
static float vm_return;
#define OFS_PARM0 0
#define OFS_RETURN 0
#define G_INT(n) vm_arg
#define G_FLOAT(n) vm_return
static void PR_SwitchQCVM(qcvm_t *vm) { qcvm = vm; }
static int PR_MakeTempString(const char *s) { (void)s; return 0; }
static void PR_ExecuteProgram(int f) { (void)f; ++vm_calls; }
static void Con_SafePrintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(output + strlen(output), sizeof(output) - strlen(output), fmt, args);
    va_end(args);
}
#define Con_Printf Con_SafePrintf
#define Con_DPrintf Con_SafePrintf
static const char *COM_TintSubstring(const char *name, const char *sub, char *buf, size_t len) {
    (void)sub; (void)buf; (void)len; return name;
}
static cvar_t *Cvar_FindVarAfter(const char *name, int flags) {
    (void)name; (void)flags; return cvars;
}
void Cmd_TokenizeString(const char *text) {
    argc = sscanf(text, "%4095s", token) == 1;
}
int Cmd_Argc(void) { return argc; }
const char *Cmd_Argv(int i) { return i == 0 ? token : ""; }
void Cbuf_InsertText(const char *text) { snprintf(inserted, sizeof(inserted), "%s", text); }
static qboolean Cvar_Command(void) {
    for (cvar_t *var = cvars; var; var = var->next)
        if (!strcmp(token, var->name)) { ++cvar_reads; return true; }
    return false;
}
static void execute(void) { ++executed; }
'''
source += function("qboolean Cmd_IsReservedName (")
source += function("char* COM_TintString(", common)
start = cmd.index("#define CMD_MAX_SUGGESTIONS")
source += cmd[start:cmd.index("/*\n============\nCmd_Apropos_f", start)]
source += function("qboolean\tCmd_ExecuteString (")
source += r'''
static void run(const char *text, cmd_source_t src) {
    output[0] = inserted[0] = 0;
    executed = cvar_reads = vm_calls = 0;
    Cmd_ExecuteString(text, src);
}
static void expect_typo(const char *text, const char *name) {
    run(text, src_command);
    assert(strstr(output, "Unknown command"));
    const char *suggested = strstr(output, "Did you mean ");
    assert(suggested);
    suggested += strlen("Did you mean ");
    for (size_t i = 0; name[i]; ++i)
        assert((unsigned char)suggested[i] == ((unsigned char)name[i] | 128));
    assert(!strcmp(suggested + strlen(name), "?\n"));
    assert(!executed && !cvar_reads && !vm_calls && !*inserted);
}
int main(void) {
    cmd_function_t commands[] = {
        {.name = "connect", .srctype = src_command, .function = execute, .next = &commands[1]},
        {.name = "connect", .srctype = src_client, .function = execute, .next = &commands[2]},
        {.name = "disconnect", .srctype = src_command, .function = execute, .next = &commands[3]},
        {.name = "private", .srctype = src_server, .function = execute, .next = &commands[4]},
        {.name = "__hidden", .srctype = src_command, .function = execute, .next = &commands[5]},
        {.name = "net_connecttimeout", .srctype = src_command, .next = &commands[6]},
        {.name = "map", .srctype = src_command, .next = &commands[7]},
        {.name = "campaign", .srctype = src_command, .next = &commands[8]},
        {.name = "status", .srctype = src_command, .next = &commands[9]},
        {.name = "net_stats", .srctype = src_command},
    };
    cvar_t vars[] = {{.name = "sensitivity", .string = "3", .next = &vars[1]},
        {.name = "host_maxfps", .string = "144", .next = &vars[2]},
        {.name = "gl_texturemode", .string = "GL_LINEAR"}};
    cmdalias_t aliases[] = {{.name = "rocketjump", .value = "echo rocket\n"}};
    cmd_functions = commands; cvars = vars; cmd_alias = aliases;
    expect_typo("conenct 127.0.0.1", "connect");  // transposition
    expect_typo("conect", "connect");           // omission
    expect_typo("connnect", "connect");         // insertion
    expect_typo("connext", "connect");          // substitution
    expect_typo("CONENCT", "connect");          // case-insensitive
    expect_typo("sensitivty 4", "sensitivity");
    expect_typo("host_maxfpz 200", "host_maxfps");
    expect_typo("rokcetjump", "rocketjump");
    expect_typo("senzitivty", "sensitivity");   // two edits for longer names
    expect_typo("connectt", "connect");         // incidental substring must not win
    expect_typo("mpa", "map");
    expect_typo("amp", "map");
    expect_typo("mp", "map");                  // short deletion
    expect_typo("stats", "status");
    expect_typo("gltexturmod", "gl_texturemode"); // three omissions in a long name
    run("con", src_command);                    // deliberate prefix stays a search
    assert(strstr(output, "containing 'con'") && !strstr(output, "Did you mean"));
    run("gl_", src_command);
    assert(strstr(output, "containing 'gl_'") && !strstr(output, "Did you mean"));
    run("connect", src_command); assert(executed == 1 && !*output);
    run("rocketjump", src_command); assert(!strcmp(inserted, "echo rocket\n") && !*output);
    run("sensitivity 4", src_command); assert(cvar_reads == 1 && !*output);
    run("conenct", src_server); assert(!*output && !executed);
    run("conenct", src_client); assert(!strstr(output, "Did you mean"));
    in_cfg_exec = true;
    run("conenct", src_command); assert(!strcmp(output, "Unknown command \"conenct\"\n"));
    in_cfg_exec = false;
    cmd_warncmd.value = 0;
    run("conenct", src_command); assert(!*output);
    developer.value = 1;
    expect_typo("conenct", "connect");
    developer.value = 0; cmd_warncmd.value = 1;
    run("privat", src_command); assert(!strstr(output, "Did you mean"));
    run("__hiden", src_command); assert(!strstr(output, "Did you mean"));
    run("zqx", src_command); assert(!strstr(output, "Did you mean"));
    run("r_zzzzzzzz", src_command); assert(!strstr(output, "Did you mean"));
    run("qz", src_command); assert(!strstr(output, "Did you mean"));
    run("connectivity", src_command); assert(!strstr(output, "Did you mean"));
    char longname[4096]; memset(longname, 'a', sizeof(longname) - 1); longname[4095] = 0;
    run(longname, src_command); assert(!strstr(output, "Did you mean"));
    run("sens", src_command); assert(strstr(output, "sensitivity (current value: \"3\")"));
    run("rocket", src_command); assert(strstr(output, "rocketjump (alias)"));
    assert(!strstr(output, "Did you mean"));
    output[0] = 0;
    assert(Cmd_ListAllContaining("zqx", 0) == 0);
    assert(strstr(output, "no cvars/commands/aliases contain"));

    // Ties are deterministic, deduplicated and capped; a closer name replaces them.
    const char *names[] = {"aat", "bat", "cat", "dat", "eat", "fat", "gat"};
    cmd_suggestions_t ties = {.distance = 1};
    for (int i = 6; i >= 0; --i) Cmd_ConsiderSuggestion(&ties, "zat", names[i]);
    Cmd_ConsiderSuggestion(&ties, "zat", "CAT");
    assert(ties.count == CMD_MAX_TYPO_SUGGESTIONS);
    assert(ties.more);
    for (int i = 0; i < ties.count; ++i) assert(!strcmp(ties.names[i], names[i]));
    Cmd_ConsiderSuggestion(&ties, "zat", "zat");
    assert(ties.count == 1 && !strcmp(ties.names[0], "zat"));
    assert(!ties.more);
    assert(Cmd_NameDistance("+atack", "-attack", 2) > 2);
    assert(Cmd_NameDistance("+atack", "+attack", 2) == 1);
    assert(Cmd_NameDistance("", "", 2) == 0);
    char boundary[MAXCMDLINE]; memset(boundary, 'a', sizeof(boundary)); boundary[255] = 0;
    assert(Cmd_NameDistance(boundary, boundary, 2) == 0);
    boundary[254] = 'b';
    assert(Cmd_NameDistance(boundary, longname, 2) > 2);

    // Multiple matches retain a list; only the names use the alternate font.
    cmd_function_t choices[] = {
        {.name = "bat", .srctype = src_command, .next = &choices[1]},
        {.name = "cat", .srctype = src_command},
    };
    cmd_functions = choices;
    run("dat", src_command);
    assert(!strcmp(output, "Unknown command \"dat\"\nDid you mean one of these?\n"
        "   \xe2\xe1\xf4\n   \xe3\xe1\xf4\n"));

    cmd_function_t crowded[7] = {0};
    for (int i = 0; i < 7; ++i) {
        crowded[i].name = names[i]; crowded[i].srctype = src_command;
        if (i) crowded[i - 1].next = &crowded[i];
    }
    cmd_functions = crowded;
    run("zat", src_command);
    assert(strstr(output, "...more equally close matches"));
    assert(!strstr(output, "Did you mean \xe1")); // never claim an arbitrary winner

    // The substring cap counts every match; explicit apropos remains uncapped.
    cmd_function_t many[CMD_MAX_SUGGESTIONS + 1] = {0};
    for (int i = 0; i < (int)(sizeof(many) / sizeof(*many)); ++i) {
        many[i].name = "capmatch"; many[i].srctype = src_command;
        if (i) many[i - 1].next = &many[i];
    }
    cmd_functions = many; output[0] = 0;
    assert(Cmd_ListAllContaining("capmatch", CMD_MAX_SUGGESTIONS) == CMD_MAX_SUGGESTIONS + 1);
    assert(strstr(output, "...and 1 more"));
    output[0] = 0;
    assert(Cmd_ListAllContaining("capmatch", 0) == CMD_MAX_SUGGESTIONS + 1);
    assert(!strstr(output, "...and"));
    puts("Command typos: dispatch, commands/cvars/aliases, edits, ties, caps and boundaries passed");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-command-typos-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(source)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c99", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I", str(ROOT / "Quake"), str(path / "test.c"), "-o", str(path / "test"),
    ], check=True)
    # This harness allocates no heap memory; skip leak scanning, which cannot
    # run under the ptrace-based sandbox. Address/undefined checks remain on.
    env = os.environ.copy()
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    subprocess.run([str(path / "test")], check=True, env=env)
