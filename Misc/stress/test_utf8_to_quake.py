"""Exercise production UTF-8 fallback, SDL typing and clipboard adapters.

Run with python3 Misc/stress/test_utf8_to_quake.py. Requires a C compiler;
uses ASan/UBSan, with no SDL installation, window, or game assets needed.
"""

from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


def cstring(text):
    data = text.encode("utf-8") if isinstance(text, str) else text
    return '"' + "".join(f"\\{byte:03o}" for byte in data) + '"'


common = (ROOT / "Quake/common.c").read_text()
header = (ROOT / "Quake/common.h").read_text()
start = common.index("static uint32_t UTF8_ReadCodePoint")
end = common.index("\nvoid SetChatInfo", start)
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define countof(a) (sizeof(a) / sizeof((a)[0]))
typedef enum { false, true } qboolean;
#define USE_SDL2 1
#define MAX_CLIPBOARDTXT 256
'''
source += re.search(r"^#define UTF8_QUAKE_BUFSIZE .*", header, re.MULTILINE).group(0) + "\n"
source += common[start:end]
source += r'''
static const char *clipboard;
static char clipboard_written[512];
static int outstanding_allocations;
static char typed[256];
static size_t typed_len;
void *Z_Malloc(int size) {
    assert(size > 0 && size <= MAX_CLIPBOARDTXT);
    void *p = calloc(1, (size_t)size);
    assert(p);
    return p;
}
char *SDL_GetClipboardText(void) {
    if (!clipboard) return NULL;
    char *copy = malloc(strlen(clipboard) + 1);
    assert(copy);
    strcpy(copy, clipboard);
    ++outstanding_allocations;
    return copy;
}
void SDL_free(void *p) { --outstanding_allocations; free(p); }
void *SDL_malloc(size_t size) {
    void *p = malloc(size);
    assert(p);
    ++outstanding_allocations;
    return p;
}
int SDL_SetClipboardText(const char *text) {
    assert(strlen(text) < sizeof(clipboard_written));
    strcpy(clipboard_written, text);
    clipboard = clipboard_written;
    return 0;
}
void Char_Event(int ch) {
    assert(ch > 0 && ch < 128 && typed_len + 1 < sizeof(typed));
    typed[typed_len++] = (char)ch;
    typed[typed_len] = 0;
}
'''
for platform in ("linux", "win"):
    platform_source = (ROOT / f"Quake/pl_{platform}.c").read_text()
    source += function(platform_source, "char *PL_GetClipboardData (void)").replace(
        "PL_GetClipboardData", f"Clipboard_{platform}"
    )

input_source = (ROOT / "Quake/in_sdl.c").read_text()
start = input_source.index("\t\tcase SDL_TEXTINPUT:")
start = input_source.index("\t\t\t{", start)
end = input_source.index("\n\t\t\t}", start) + len("\n\t\t\t}")
source += r'''
static void type_text(const char *input) {
    struct { struct { char text[32]; } text; } event;
    assert(strlen(input) < sizeof(event.text.text));
    strcpy(event.text.text, input);
    typed_len = 0;
    typed[0] = 0;
'''
source += input_source[start:end] + "\n}\n"

menu = (ROOT / "Quake/menu.c").read_text()
source += r'''
#define CLAMP(a, b, c) ((b) < (a) ? (a) : (b) > (c) ? (c) : (b))
typedef struct {
    char *text;
    int max_len, cursor, sel_start;
    qboolean digits_only;
} menu_textfield_t;
'''
for declaration in (
    "void M_TextField_ClampCursor(",
    "void M_TextField_Init(",
    "static qboolean M_TextField_GetSelection(",
    "static qboolean M_TextField_DeleteRange(",
    "static qboolean M_TextField_DeleteSelection(",
    "static qboolean M_TextField_Insert(",
    "static void M_TextField_InsertUTF8(",
):
    source += function(menu, declaration)

# Copy must export UTF-8/ASCII too: existing names may use high-bit Quake glyphs.
source += "static char dequake[256];\n"
source += function((ROOT / "Quake/host.c").read_text(), "void Host_InitDeQuake (void)")
source += "static void M_TextField_PlayCopySound(void) {}\n"
source += function(menu, "static qboolean M_TextField_CopySelection(")
source += r'''
#define MAX_CHAT_SIZE_EX 100
static char chat_buffer[MAX_CHAT_SIZE_EX];
static int selection_start, selection_end;
static qboolean Key_GetChatSelection(int *start, int *end) {
    *start = selection_start;
    *end = selection_end;
    return *start >= 0 && *start < *end;
}
'''
source += function((ROOT / "Quake/keys.c").read_text(), "static qboolean Chat_CopySelectionToClipboard (void)")

joiner_cases = [
    ("a\u200db", "ab"),
    ("\u200dhello", "hello"),
    ("hello\u200d", "hello"),
    ("\u200d", ""),
    ("a\u200d\u200db", "ab"),
    ("a\u200d b", "a b"),
    ("a\u200d\nb", "a\nb"),
    ("\u200dé", "e"),
    ("😶\u200d🌫️", ":|?"),  # ignore the joiner, preserve each component
    ("👨\u200d👩\u200d👧\u200d👦", "????"),
    ("❤️\u200d🔥", "<3[fire]"),
]

cases = [
    ("", ""),
    ("Quake ^1RED ^^ name 123\n\r\t\b", "Quake ^1RED ^^ name 123\n\r\t\b"),
    ("café déjà vu — “hello”…", 'cafe deja vu - "hello"...'),
    ("Cafe\u0301 A\u030a", "Cafe A"),
    ("Ångström Łódź Æsir Straße Œuf", "Angstrom Lodz AEsir Strasse OEuf"),
    ("ＡＢＣ１２３！", "ABC123!"),
    ("a\u00a0b\u202fc", "a b c"),
    ("🙂 ❤️ 👍🏽", ":) <3 [+1]"),
    ("😂 😉 😎 😭 😡 😜", ":D ;) B) :'( >:( :P"),
    ("✅ ❌ ⚠️ 🔥 💀 🎉 👋 🙏 💯 🏆", "[ok] [x] [!] [fire] [skull] [party] [wave] [thanks] 100 [win]"),
    ("中 🦄", "? ?"),
    # Invisible characters decorate their neighbours; they must not become "?".
    ("\ufeffhi\u00ade\u200bre\u202a!\u2069", "hiere!"),
    ("25\u00b0C \u00bd \u00a9 \u2122 \u20ac5 \u00b1 \u2260 \u2265", "25degC 1/2 (c) (tm) E5 +/- != >="),
    ("\u00bfque\u0301 tal\u203d \u00a1ole!", "?que tal?! !ole!"),
    ("\u3000a\u2003b\u2028c", " a b c"),
    ("\U0001f1fa\U0001f1f8 vs \U0001f1e9\U0001f1ea", "US vs DE"),
    ("\u041f\u0440\u0438\u0432\u0435\u0442, \u0416\u0435\u043d\u044f!", "Privet, Zhenya!"),
    ("\u0429\u0438\u0442 \u044a \u044c", "Shchit  "),
    ("\u0391\u03b8\u03ae\u03bd\u03b1 \u03a8\u03c2", "Athina Pss"),
    ("\U0001f410 \U0001f680 \u26a1 \U0001f4aa \U0001f3af \U0001f440",
     "[goat] [rocket] [zap] [flex] [target] [eyes]"),
    ("\U0001f914 \U0001f60d \U0001f610 \U0001f631 \U0001f937",
     "[hmm] <3 :| :o [shrug]"),
    (b"a\xc3", "a?"),
    (b"\xe2\x82", "?"),
    (b"\xf0\x9f\x98", "?"),
    (b"\xe2\x82X", "?X"),
    (b"\xc0\xaf", "??"),  # overlong slash must not become a slash
    (b"\xed\xa0\x80", "?"),  # surrogate
    (b"\xf4\x90\x80\x80", "?"),  # above Unicode range
    (b"\xe0\x80\xaf", "?"),
    (b"\x80\xffA", "??A"),
] + joiner_cases
source += "static const struct { const char *input, *expected; } cases[] = {\n"
source += "".join(f"    {{{cstring(a)}, {cstring(b)}}},\n" for a, b in cases)
source += "};\n"
source += "static const struct { const char *input, *expected; } joiner_cases[] = {\n"
source += "".join(f"    {{{cstring(a)}, {cstring(b)}}},\n" for a, b in joiner_cases)
source += "};\n"
source += r'''
static void encode_utf8(char *dst, uint32_t cp) {
    if (cp < 0x80) { *dst++ = (char)cp; }
    else if (cp < 0x800) {
        *dst++ = (char)(0xc0 | (cp >> 6));
        *dst++ = (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        *dst++ = (char)(0xe0 | (cp >> 12));
        *dst++ = (char)(0x80 | ((cp >> 6) & 0x3f));
        *dst++ = (char)(0x80 | (cp & 0x3f));
    } else {
        *dst++ = (char)(0xf0 | (cp >> 18));
        *dst++ = (char)(0x80 | ((cp >> 12) & 0x3f));
        *dst++ = (char)(0x80 | ((cp >> 6) & 0x3f));
        *dst++ = (char)(0x80 | (cp & 0x3f));
    }
    *dst = 0;
}

static void check_bounds(const char *input) {
    for (size_t capacity = 0; capacity < 40; ++capacity) {
        char *out = malloc(capacity + 1);
        assert(out);
        memset(out, 0x55, capacity + 1);
        size_t n = UTF8_ToQuake(out, capacity, input);
        assert(out[capacity] == 0x55);
        if (capacity) {
            assert(n < capacity && out[n] == 0 && strlen(out) == n);
            for (size_t j = 0; j < n; ++j)
                assert((unsigned char)out[j] < 128);
        } else assert(n == 0);
        free(out);
    }
}
int main(void) {
    char out[1024];
    Host_InitDeQuake();
    assert(UTF8_ToQuake(NULL, 0, "anything") == 0);
    for (size_t i = 0; i < countof(cases); ++i) {
        /* Allocate only the input plus NUL, so ASan catches decoder overreads. */
        char *input = malloc(strlen(cases[i].input) + 1);
        assert(input);
        strcpy(input, cases[i].input);
        size_t n = UTF8_ToQuake(out, sizeof(out), input);
        if (strcmp(out, cases[i].expected)) {
            fprintf(stderr, "case %zu: got <%s>, wanted <%s>\n", i, out, cases[i].expected);
            abort();
        }
        assert(n == strlen(cases[i].expected));
        check_bounds(input);
        clipboard = input;
        char *linux_text = Clipboard_linux(), *windows_text = Clipboard_win();
        assert(!strcmp(linux_text, cases[i].expected));
        assert(!strcmp(windows_text, cases[i].expected));
        assert(outstanding_allocations == 0);
        free(linux_text);
        free(windows_text);
        if (strlen(input) < 32) {
            type_text(input);
            assert(!strcmp(typed, cases[i].expected));
        }
        free(input);
    }
    /* SDL may split joined emoji between events, but not within a codepoint.
       Every such split must preserve the same text as a single event/paste. */
    for (size_t i = 0; i < countof(joiner_cases); ++i) {
        const char *input = joiner_cases[i].input;
        size_t len = strlen(input);
        for (size_t split = 0; split <= len; ++split) {
            char first[32], combined[512];
            if (((unsigned char)input[split] & 0xc0) == 0x80)
                continue;
            assert(split < sizeof(first));
            memcpy(first, input, split);
            first[split] = 0;
            type_text(first);
            strcpy(combined, typed);
            type_text(input + split);
            strcat(combined, typed);
            assert(!strcmp(combined, joiner_cases[i].expected));
        }
    }
    clipboard = NULL;
    assert(Clipboard_linux() == NULL && Clipboard_win() == NULL);
    /* Strict ordering is necessary for lookup; input events allow 3x expansion. */
    for (size_t i = 0; i < countof(unicode_quake_fallbacks); ++i) {
        uint32_t cp = unicode_quake_fallbacks[i].codepoint;
        size_t utf8_len = cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        assert(memchr(unicode_quake_fallbacks[i].text, 0, UTF8_QUAKE_BUFSIZE));
        assert(strlen(unicode_quake_fallbacks[i].text) <= utf8_len * 3);
        for (const char *p = unicode_quake_fallbacks[i].text; *p; ++p)
            assert(*p >= 32 && *p <= 126);
        if (i) assert(unicode_quake_fallbacks[i - 1].codepoint < cp);
        /* Every table entry must survive a round trip through the decoder,
           so no mapping is shadowed by the invisible or ASCII fast paths. */
        char encoded[8], converted[UTF8_QUAKE_BUFSIZE];
        const char *cursor = encoded;
        encode_utf8(encoded, cp);
        assert(UTF8_ToQuakeChar(converted, &cursor) == strlen(unicode_quake_fallbacks[i].text));
        assert(!strcmp(converted, unicode_quake_fallbacks[i].text) && !*cursor);
    }
    /* Validate every scalar value, including the edges of each UTF-8 length.
       The iterator must consume exactly one codepoint, never the next letter. */
    for (uint32_t cp = 1; cp <= 0x10ffff; ++cp) {
        if (cp >= 0xd800 && cp <= 0xdfff) continue;
        char encoded[8], converted[UTF8_QUAKE_BUFSIZE];
        encode_utf8(encoded, cp);
        size_t bytes = strlen(encoded);
        strcpy(encoded + bytes, "X");
        const char *cursor = encoded;
        assert(UTF8_ReadCodePoint(&cursor) == cp && cursor == encoded + bytes);
        cursor = encoded;
        size_t n = UTF8_ToQuakeChar(converted, &cursor);
        assert(cursor == encoded + bytes && *cursor == 'X');
        assert(n < sizeof(converted) && converted[n] == 0 && strlen(converted) == n);
        for (size_t j = 0; j < n; ++j) assert((unsigned char)converted[j] < 128);
    }
'''
source += f"assert(UTF8_ToQuake(out, 5, {cstring('a👍b')}) == 1 && !strcmp(out, \"a\"));\n"
source += f"assert(UTF8_ToQuake(out, 6, {cstring('a👍b')}) == 5 && !strcmp(out, \"a[+1]\"));\n"
source += f"type_text({cstring('🙏' * 7)}); assert(!strcmp(typed, {cstring('[thanks]' * 7)}));\n"
source += r'''
    menu_textfield_t tf;
    char field[32] = "";
    M_TextField_Init(&tf, field, 3, true);
    M_TextField_InsertUTF8(&tf, "ignored text 123456");
    assert(!strcmp(field, "123"));
    tf.sel_start = 0;
    M_TextField_InsertUTF8(&tf, "ignored text 456789");
    assert(!strcmp(field, "456"));
    strcpy(field, "abcd");
    M_TextField_Init(&tf, field, 6, false);
    tf.cursor = 3;
    tf.sel_start = 1;
'''
source += f"M_TextField_InsertUTF8(&tf, {cstring('é🙂')}); assert(!strcmp(field, \"ae:)d\"));\n"
source += r'''
    field[0] = 0;
    M_TextField_Init(&tf, field, 3, false);
    M_TextField_InsertUTF8(&tf, "\n\r\t123456");
    assert(!strcmp(field, "123"));
    /* Copying an old coloured name must produce valid plain text for paste. */
    strcpy(chat_buffer, "\310\351"); // high-bit H, i
    selection_start = 0;
    selection_end = 2;
    assert(Chat_CopySelectionToClipboard());
    assert(!strcmp(clipboard_written, "Hi"));
    char *copied = Clipboard_linux();
    assert(!strcmp(copied, "Hi"));
    free(copied);
    selection_start = -1;
    assert(!Chat_CopySelectionToClipboard());
    strcpy(field, "\310\351\022\200\201\202"); // name, gold zero, Quake bar
    M_TextField_Init(&tf, field, (int)sizeof(field) - 1, false);
    tf.sel_start = 0;
    assert(M_TextField_CopySelection(&tf));
    assert(!strcmp(clipboard_written, "Hi0(=)"));
    field[0] = 0;
    M_TextField_Init(&tf, field, (int)sizeof(field) - 1, false);
    M_TextField_InsertUTF8(&tf, clipboard_written);
    assert(!strcmp(field, "Hi0(=)"));
    assert(outstanding_allocations == 0);
'''
source += r'''
    /* Clipboard conversion happens before output truncation. */
    char huge[4096];
    memset(huge, 'a', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = 0;
    clipboard = huge;
    char *limited = Clipboard_linux();
    assert(strlen(limited) == 255);
    free(limited);
    limited = Clipboard_win();
    assert(strlen(limited) == 255);
    free(limited);
    /* Random malformed sequences, exact source allocations, all small capacities. */
    uint32_t rng = 1;
    for (size_t iteration = 0; iteration < 10000; ++iteration) {
        size_t len = iteration % 32;
        char *input = malloc(len + 1);
        assert(input);
        for (size_t j = 0; j < len; ++j) {
            rng = rng * 1664525u + 1013904223u;
            input[j] = (char)(1 + (rng >> 16) % 255);
        }
        input[len] = 0;
        check_bounds(input);
        free(input);
    }
    puts("UTF-8 fallback: mappings, SDL typing, clipboard, truncation and malformed input passed");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-utf8-") as directory:
    cfile = Path(directory) / "test.c"
    binary = Path(directory) / "test"
    cfile.write_text(source)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c99", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        str(cfile), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
