#!/usr/bin/env python3
"""Exercise the production remote-command trust code natively under ASan/UBSan.

Covers the paths the hostile-server test cannot reach without a QuakeC compiler
or a keyboard: command-buffer origin marks and bounds, bind origin and config
persistence, CSQC key-up delivery, QC cvar reads and file-read rules,
command registration over protected cvars, and secret display.

Run with python3 Misc/stress/test_remote_command_native.py (requires a C compiler).
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
Q = ROOT / "Quake"
cmd = (Q / "cmd.c").read_text()
cvar = (Q / "cvar.c").read_text()
keys = (Q / "keys.c").read_text()
pr_ext = (Q / "pr_ext.c").read_text()
pr_cmds = (Q / "pr_cmds.c").read_text()
cvar_flags = "\n".join(l for l in (Q / "cvar.h").read_text().splitlines() if re.match(r"#define\s+CVAR_", l))


def function(declaration, text):
    start = text.index(declaration)
    return text[start:text.index("\n}", start) + 2] + "\n"


def span(start_marker, end_marker, text):
    start = text.index(start_marker)
    return text[start:text.index(end_marker, start)] + "\n"


source = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef unsigned char byte;
typedef enum { false, true } qboolean;
#include "cmd.h"
''' + cvar_flags + r'''
#define MAX_BINDMAPS 2
#define MAX_KEYS 320
#define STRINGTEMP_LENGTH 1024
#define OFS_RETURN 1
#define OFS_PARM0 4
#define OFS_PARM1 7
#define OFS_PARM2 10
#define OFS_PARM3 13
#define CSIE_KEYDOWN 0
#define CSIE_KEYUP 1
#define q_strcasecmp strcasecmp
#define q_strncasecmp strncasecmp
#define Q_strlen(s) ((int)strlen(s))
#define Q_memcpy memcpy
#define Q_strcmp strcmp

typedef struct cvar_s {
    const char *name, *string;
    unsigned int flags;
    float value;
    const char *default_string;
    void (*help)(struct cvar_s *);
} cvar_t;
typedef struct { byte *data; int maxsize, cursize; } sizebuf_t;
typedef struct { struct { int CSQC_InputEvent; } extfuncs; } qcvm_t;
typedef enum { key_game, key_console, key_message, key_menu } keydest_t;

static qcvm_t *qcvm;
static struct { qcvm_t qcvm; } cl;
static char out[1 << 16];
static jmp_buf fatal;
static int host_initialized = 1;
static int executed_count;
static char executed[64][128];
static qboolean executed_untrusted[64];

static void Con_Printf(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vsnprintf(out + strlen(out), sizeof(out) - strlen(out), fmt, a);
    va_end(a);
}
#define Con_SafePrintf Con_Printf
static void SZ_Alloc(sizebuf_t *b, int size) { b->data = malloc(size); b->maxsize = size; b->cursize = 0; }
static void SZ_Clear(sizebuf_t *b) { b->cursize = 0; }
static void SZ_Write(sizebuf_t *b, const void *d, int l) {
    if (b->cursize + l > b->maxsize) longjmp(fatal, 1);  /* production: Sys_Error */
    memcpy(b->data + b->cursize, d, l); b->cursize += l;
}
static void *Hunk_AllocName(int size, const char *name) { (void)name; return malloc(size); }
static void *Hunk_Alloc(int size) { return calloc(1, size); }
static void Sys_Error(const char *fmt, ...) { (void)fmt; abort(); }
static void *Z_Malloc(int size) { return calloc(1, size); }
static void Z_Free(void *p) { free(p); }
static char *Z_Strdup(const char *s) { return strdup(s); }
static qboolean cmd_wait;

/* Cmd_ExecuteString: records each line with its origin; a few lines act. */
qboolean Cmd_ExecuteString(const char *text, cmd_source_t src) {
    (void)src;
    assert(executed_count < 64);
    snprintf(executed[executed_count], sizeof(executed[0]), "%s", text);
    executed_untrusted[executed_count++] = Cmd_IsUntrusted();
    if (!strcmp(text, "insert")) Cbuf_InsertText("inserted");
    if (!strcmp(text, "wait")) cmd_wait = true;
    return true;
}
'''
source += span("sizebuf_t\tcmd_text;", "/*\n==============================================================================\n\n\t\t\t\t\t\tSCRIPT COMMANDS", cmd)

# Cvar lookups, gamecode reads and display.
source += r'''
static cvar_t cvars[] = {
    {.name = "rcon_password", .string = "1234", .flags = CVAR_SECRET, .value = 1234, .default_string = ""},
    {.name = "rcon_address", .string = "", .flags = CVAR_PROTECTED, .default_string = ""},
    {.name = "sensitivity", .string = "3", .value = 3, .default_string = "3"},
};
cvar_t *Cvar_FindVar(const char *name) {
    for (size_t i = 0; i < sizeof(cvars) / sizeof(cvars[0]); i++)
        if (!strcmp(cvars[i].name, name)) return &cvars[i];
    return NULL;
}
const char *Cvar_VariableString(const char *name) { cvar_t *v = Cvar_FindVar(name); return v ? v->string : ""; }
static int argc; static const char *argv[4];
int Cmd_Argc(void) { return argc; }
const char *Cmd_Argv(int i) { return i < argc ? argv[i] : ""; }
static qboolean redirected;
static qboolean Con_IsRedirected(void) { return redirected; }
static int sets;
static void Cvar_Set(const char *n, const char *v) { (void)n; (void)v; sets++; }
static float globals[32];
static const char *strings[32];
static int nstrings;
#define G_FLOAT(o) globals[o]
#define G_INT(o) (*(int *)&globals[o])
#define G_STRING(o) strings[(int)globals[o]]
#define G_VECTORSET(o, a, b, c) (globals[o] = (a), globals[(o) + 1] = (b), globals[(o) + 2] = (c))
static char tempstring[STRINGTEMP_LENGTH];
static char *PR_GetTempString(void) { return tempstring; }
static int PR_SetEngineString(const char *s) { strings[++nstrings] = s; return nstrings; }
static int setstr(const char *s) { strings[++nstrings] = s; return nstrings; }
static const char *COM_GetGameNames(qboolean full) { (void)full; return "id1"; }
static size_t q_strlcpy(char *d, const char *s, size_t n) { snprintf(d, n, "%s", s); return strlen(s); }
'''
source += function("cvar_t *Cvar_FindVarForGamecode (", cvar)
source += function("static const char *Cvar_DisplayString (const cvar_t *var, const char *value)\n{", cvar)
source += function("qboolean\tCvar_Command (void)", cvar)
source += function("static void PF_cvar (void)", pr_cmds)
source += function("static void PF_cvar_string(void)", pr_ext)
source += function("static void PF_cvar_defstring(void)", pr_ext)

# QC file-name rule.
source += r'''
static const char *COM_FileGetExtension(const char *in) {
    const char *dot = strrchr(in, '.'), *slash = strrchr(in, '/');
    return (dot && (!slash || dot > slash)) ? dot + 1 : "";
}
static char vabuf[256];
static const char *va(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vsnprintf(vabuf, sizeof(vabuf), fmt, a); va_end(a); return vabuf;
}
'''
source += function("static qboolean QC_PathIsCanonical(", pr_ext)
source += function("static qboolean QC_FixFileName(", pr_ext)

# Command registration.
source += "static cmd_function_t *cmd_functions;\n"
source += function("cmd_function_t *Cmd_AddCommand2 (", cmd)

# Binds and CSQC key events.
source += r'''
static char *keybindings[MAX_BINDMAPS][MAX_KEYS];
static qboolean keybinding_untrusted[MAX_BINDMAPS][MAX_KEYS];
static char *keybinding_saved[MAX_BINDMAPS][MAX_KEYS];
static int key_bindmap[2] = {0, 1};
static keydest_t key_dest;
static struct { float value; } cfg_unbindall;
static const char *Key_KeynumToString(int k) { static char b[8]; snprintf(b, sizeof(b), "k%d", k); return b; }
static int Key_NativeToQC(int k) { return k; }
static int csqc_events, csqc_last_down;
static void PR_SwitchQCVM(qcvm_t *vm) { qcvm = vm; }
static void PR_ExecuteProgram(int f) { (void)f; csqc_events++; csqc_last_down = globals[OFS_PARM0] == CSIE_KEYDOWN; }
'''
source += function("static const char *Key_GetEffectiveBindingOrigin (", keys)
source += function("static void Key_AddBindingText (", keys)
source += function("void Key_SetBinding (", keys)
source += function("void Key_WriteBindings (FILE *f)", keys)
source += span("static qboolean csqc_keydown[MAX_KEYS];", "\n}\n", keys) + "\n}\n"

source += r'''
static void reset(void) { out[0] = 0; executed_count = 0; }
static void run_buffer(void) { Cbuf_Execute(); }

static void test_cbuf(void) {
    Cbuf_Init();
    /* Origin marks follow bytes: one untrusted byte taints its whole line. */
    reset();
    Cbuf_AddTextUntrusted("a\n"); Cbuf_AddText("b;c\n");
    Cbuf_AddTextUntrusted("rc"); Cbuf_AddText("on x\n");
    run_buffer();
    assert(executed_count == 4);
    assert(!strcmp(executed[0], "a") && executed_untrusted[0]);
    assert(!strcmp(executed[1], "b") && !executed_untrusted[1]);
    assert(!strcmp(executed[2], "c") && !executed_untrusted[2]);
    assert(!strcmp(executed[3], "rcon x") && executed_untrusted[3]);
    assert(!Cmd_IsUntrusted());

    /* Inserted text inherits the running line's origin; queued text keeps its own. */
    reset();
    Cbuf_AddTextUntrusted("insert\n"); Cbuf_AddText("insert\nlocal\n");
    run_buffer();
    assert(executed_count == 5);
    assert(!strcmp(executed[1], "inserted") && executed_untrusted[1]);
    assert(!strcmp(executed[3], "inserted") && !executed_untrusted[3]);
    assert(!strcmp(executed[4], "local") && !executed_untrusted[4]);

    /* wait keeps the remaining marks for the next frame. */
    reset();
    Cbuf_AddTextUntrusted("wait;after\n"); Cbuf_AddText("mine\n");
    run_buffer(); assert(executed_count == 1);
    cmd_wait = false; run_buffer();
    assert(executed_count == 3 && executed_untrusted[1] && !executed_untrusted[2]);

    /* CSQC is untrusted while it runs. */
    reset(); qcvm = &cl.qcvm; Cbuf_AddText("csqc\n"); qcvm = NULL;
    run_buffer(); assert(executed_count == 1 && executed_untrusted[0]);

    /* Error recovery clears a context a longjmp skipped restoring. */
    Cmd_EnterUntrusted(); assert(Cmd_IsUntrusted()); Cmd_ResetUntrusted(); assert(!Cmd_IsUntrusted());

    /* Insertion bounds: refuse cleanly when inserted + newline + queued would
     * not fit (this wrote past the origin buffer under ASan before the fix). */
    static char big[(1 << 18) + 1];
    int room;
    reset(); SZ_Clear(&cmd_text);
    memset(big, 'q', cmd_text.maxsize - 16); big[cmd_text.maxsize - 16] = 0;
    Cbuf_AddText(big);
    room = cmd_text.maxsize - cmd_text.cursize;    /* 16 */
    memset(big, 'i', 64); big[64] = 0;
    if (setjmp(fatal)) assert(!"insert overflowed into SZ_Write");
    Cbuf_InsertText(big);
    assert(strstr(out, "Cbuf_InsertText: overflow") && cmd_text.cursize == cmd_text.maxsize - 16);
    /* Exactly fitting: text + newline + queued == maxsize - 1. */
    big[room - 2] = 0;
    Cbuf_InsertTextUntrustedProbe:;
    out[0] = 0;
    Cbuf_InsertText(big);
    assert(!out[0] && cmd_text.cursize == cmd_text.maxsize - 1);
    assert(cmd_text.data[room - 2] == '\n' && cmd_text.data[room - 1] == 'q');
    SZ_Clear(&cmd_text);
    /* AddText at the edge is refused too, without touching the marks. */
    memset(big, 'z', cmd_text.maxsize); big[cmd_text.maxsize] = 0;     /* maxsize bytes: one too many */
    out[0] = 0; Cbuf_AddTextUntrusted(big); assert(strstr(out, "overflow") && !cmd_text.cursize);
}

static void test_cvars(void) {
    /* Gamecode never reads a secret, by value, string or default. */
    globals[OFS_PARM0] = setstr("rcon_password"); PF_cvar(); assert(globals[OFS_RETURN] == 0);
    globals[OFS_PARM0] = setstr("sensitivity"); PF_cvar(); assert(globals[OFS_RETURN] == 3);
    globals[OFS_PARM0] = setstr("rcon_password"); PF_cvar_string(); assert(G_INT(OFS_RETURN) == 0);
    globals[OFS_PARM0] = setstr("rcon_password"); PF_cvar_defstring(); assert(G_INT(OFS_RETURN) == 0);
    globals[OFS_PARM0] = setstr("sensitivity"); PF_cvar_string(); assert(!strcmp(strings[G_INT(OFS_RETURN)], "3"));

    /* Queries show the value to the user, "<hidden>" to a server. */
    argc = 1; argv[0] = "rcon_password";
    out[0] = 0; Cvar_Command(); assert(strstr(out, "\"1234\""));
    Cmd_EnterUntrusted(); out[0] = 0; Cvar_Command(); Cmd_ResetUntrusted();
    assert(strstr(out, "<hidden>") && !strstr(out, "1234"));
    argc = 2; argv[1] = "x"; redirected = true;
    Cmd_EnterUntrusted(); out[0] = 0; Cvar_Command(); Cmd_ResetUntrusted();
    assert(!strstr(out, "1234")); redirected = false;
}

static void test_files(void) {
    const char *name, *fallback;
    struct { const char *in; qboolean fallback; } cases[] = {
        {"config.cfg", false}, {"autoexec.cfg", false},
        {"configs/config.cfg", false}, {"configs/autoexec.cfg", false}, {"CONFIGS/autoexec.cfg", false},
        {"configs/bookmarks.txt", false}, {"maps/x.cfg", false},
        {"particles/fx.cfg", true}, {"huds/hud.cfg", true}, {"models/m.cfg", true},
        {"readme.txt", true}, {"maps/e1m1.ent", true},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        assert(QC_FixFileName(cases[i].in, &name, &fallback));
        assert(!strncmp(name, "data/", 5));
        if ((fallback != NULL) != cases[i].fallback) { fprintf(stderr, "fallback wrong: %s\n", cases[i].in); abort(); }
    }
    assert(!QC_FixFileName("../x", &name, &fallback) && !QC_FixFileName("/etc/passwd", &name, &fallback));
    /* Spellings that dodge the prefix/extension checks are refused outright. */
    const char *ambiguous[] = {
        "config.cfg.", "config.cfg ", "config.cfg..", "./configs/autoexec.cfg.", "./configs/bookmarks.txt",
        "configs/./autoexec.cfg", "configs//autoexec.cfg", "maps/../configs/x.txt", "configs /x.txt",
        "configs./x.txt", "CONFIG~1/bookmarks.txt", "AUTOEX~1.CFG", "x/", "/", ".", "readme.txt ",
    };
    for (size_t i = 0; i < sizeof(ambiguous) / sizeof(ambiguous[0]); i++)
        if (QC_FixFileName(ambiguous[i], &name, &fallback)) { fprintf(stderr, "accepted: '%s'\n", ambiguous[i]); abort(); }
    /* Ordinary names, including inner dots and spaces, still work. */
    const char *ordinary[] = {"readme.txt", "my file.txt", "a.b.c.txt", ".hidden/x.txt", "maps/e1m1.ent", "particles/fx.cfg"};
    for (size_t i = 0; i < sizeof(ordinary) / sizeof(ordinary[0]); i++)
        if (!QC_FixFileName(ordinary[i], &name, &fallback)) { fprintf(stderr, "refused: '%s'\n", ordinary[i]); abort(); }
}

static void test_commands(void) {
    /* registercommand can't shadow a secret or protected cvar, even an empty one. */
    cvars[0].string = "";
    out[0] = 0; assert(!Cmd_AddCommand2("rcon_password", NULL, src_command, false) && strstr(out, "protected"));
    out[0] = 0; assert(!Cmd_AddCommand2("rcon_address", NULL, src_command, false) && strstr(out, "protected"));
    assert(Cmd_AddCommand2("csqc_cmd", NULL, src_command, false));
    cvars[0].string = "1234";
}

static int added_untrusted, added_trusted;
static void test_binds(void) {
    const char *kb; qboolean u;
    FILE *f; char cfg[4096] = {0};

    Key_SetBinding(10, "echo mine", 0);                       /* user */
    Cmd_EnterUntrusted(); Key_SetBinding(10, "rcon steal", 0); Key_SetBinding(11, "rcon two", 0); Cmd_ResetUntrusted();
    kb = Key_GetEffectiveBindingOrigin(10, &u); assert(!strcmp(kb, "rcon steal") && u);
    kb = Key_GetEffectiveBindingOrigin(11, &u); assert(u);

    /* Pressing a server-bound key queues its text as untrusted. */
    reset(); SZ_Clear(&cmd_text);
    Key_AddBindingText(kb, u); Key_AddBindingText("\n", u);
    run_buffer(); assert(executed_count == 1 && executed_untrusted[0]);

    /* The config keeps the user's binding, and drops the server-only one. */
    f = fmemopen(cfg, sizeof(cfg) - 1, "w"); Key_WriteBindings(f); fclose(f);
    assert(strstr(cfg, "\"echo mine\"") && !strstr(cfg, "rcon"));

    /* A local rebind makes the key the user's again. */
    Key_SetBinding(10, "echo again", 0);
    kb = Key_GetEffectiveBindingOrigin(10, &u); assert(!u);
    memset(cfg, 0, sizeof(cfg)); f = fmemopen(cfg, sizeof(cfg) - 1, "w"); Key_WriteBindings(f); fclose(f);
    assert(strstr(cfg, "\"echo again\""));
    (void)added_untrusted; (void)added_trusted;
}

static void test_csqc_keys(void) {
    cl.qcvm.extfuncs.CSQC_InputEvent = 1;
    /* Console typing: no downs, and no ups for keys CSQC never saw go down. */
    key_dest = key_console; csqc_events = 0;
    for (int k = 'a'; k <= 'z'; k++) { CSQC_HandleKeyEvent(true, k, k); CSQC_HandleKeyEvent(false, k, k); }
    assert(csqc_events == 0);
    /* A key held in game still gets its up after the console opens. */
    key_dest = key_game; CSQC_HandleKeyEvent(true, 'w', 'w'); assert(csqc_events == 1 && csqc_last_down);
    key_dest = key_console; CSQC_HandleKeyEvent(false, 'w', 'w'); assert(csqc_events == 2 && !csqc_last_down);
    CSQC_HandleKeyEvent(false, 'w', 'w'); assert(csqc_events == 2);   /* only once */
    CSQC_HandleKeyEvent(false, -1, 0); CSQC_HandleKeyEvent(false, MAX_KEYS, 0); assert(csqc_events == 2);
}

int main(void) {
    test_cbuf();
    test_cvars();
    test_files();
    test_commands();
    test_binds();
    test_csqc_keys();
    puts("PASS: command origin marks and insert bounds, gamecode cvar reads, secret display, QC file fallback and path spellings, "
         "protected command names, bind origin and persistence, CSQC key-ups");
    return 0;
}
'''
source = source.replace("    Cbuf_InsertTextUntrustedProbe:;\n", "")

with tempfile.TemporaryDirectory(prefix="qssm-remote-native-") as tmp:
    c = Path(tmp) / "test.c"
    c.write_text(source)
    exe = Path(tmp) / "test"
    subprocess.run(["cc", "-std=gnu11", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-Wno-unused-parameter", "-Wno-sign-compare", "-Wno-missing-field-initializers",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-I", str(Q), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, env={"ASAN_OPTIONS": "detect_leaks=0"})
