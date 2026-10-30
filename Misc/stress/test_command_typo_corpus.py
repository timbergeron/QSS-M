"""Audit production suggestions against declared names and generated typos.

Run with python3 Misc/stress/test_command_typo_corpus.py. No game data is needed.
An optional --registry-json file can supply names scraped from a running engine.
This measures synthetic typo coverage, not confidence about a user's intent.
"""

from pathlib import Path
import argparse
import ctypes
import itertools
import json
import os
import re
import runpy
import shlex
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--registry-json", type=Path)
args = parser.parse_args()
# Reuse the production-code extraction and engine stubs, including their checks.
ns = runpy.run_path(str(Path(__file__).with_name("test_command_typos.py")))
if args.registry_json:
    names = sorted(set(json.loads(args.registry_json.read_text())))
else:
    names = set()
    for path in (ROOT / "Quake").glob("*.c"):
        text = path.read_text(errors="replace")
        names.update(re.findall(r'Cmd_AddCommand(?!_ServerCommand)\w*\s*\(\s*"([^"]+)"', text))
        names.update(re.findall(r'cvar_t\s+\w+\s*=\s*\{\s*"([^"\n]+)"', text))
    names = sorted(names)
names = [name for name in names if not name.startswith("__")
         and re.fullmatch(r"[A-Za-z0-9_+.-]+", name)]
assert names

wrapper = r'''
const char *audit(const char *input, const char **names, int count) {
    cmd_function_t *commands = calloc(count, sizeof(*commands));
    assert(commands);
    for (int i = 0; i < count; ++i) {
        commands[i].name = names[i]; commands[i].srctype = src_command;
        if (i + 1 < count) commands[i].next = &commands[i + 1];
    }
    cmd_functions = commands; cmd_alias = NULL; cvars = NULL;
    run(input, src_command);
    free(commands); cmd_functions = NULL;
    return output;
}
int distance(const char *a, const char *b, int limit) { return Cmd_NameDistance(a, b, limit); }
'''


def reference_distance(a, b):
    """Full-matrix OSA reference, without production's rolling rows or cutoff."""
    d = [[0] * (len(b) + 1) for _ in range(len(a) + 1)]
    for i in range(len(a) + 1):
        d[i][0] = i
    for j in range(len(b) + 1):
        d[0][j] = j
    for i in range(1, len(a) + 1):
        for j in range(1, len(b) + 1):
            d[i][j] = min(d[i - 1][j] + 1, d[i][j - 1] + 1,
                          d[i - 1][j - 1] + (a[i - 1] != b[j - 1]))
            if i > 1 and j > 1 and a[i - 2:i] == b[j - 2:j][::-1]:
                d[i][j] = min(d[i][j], d[i - 2][j - 2] + 1)
    return d[-1][-1]


with tempfile.TemporaryDirectory(prefix="qssm-typo-corpus-") as tmp:
    path = Path(tmp)
    (path / "audit.c").write_text(ns["source"] + wrapper)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
                    "-std=c99", "-O2", "-shared", "-fPIC", "-I", str(ROOT / "Quake"),
                    str(path / "audit.c"), "-o", str(path / "audit.so")], check=True)
    lib = ctypes.CDLL(str(path / "audit.so"))
    lib.audit.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int]
    lib.audit.restype = ctypes.c_char_p
    lib.distance.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    lib.distance.restype = ctypes.c_int
    encoded = (ctypes.c_char_p * len(names))(*(name.encode() for name in names))

    def audit(word):
        raw = lib.audit(word.encode(), encoded, len(names))
        return bytes(c & 127 for c in raw).decode()

    common = ["connect", "disconnect", "sensitivity", "host_maxfps", "gl_texturemode",
              "vid_width", "vid_height", "vid_fullscreen", "scr_conscale", "r_wateralpha",
              "cl_forwardspeed", "cl_backspeed", "cl_sidespeed", "crosshair", "volume",
              "bind", "unbind", "map", "exec", "quit", "status", "+attack", "-attack",
              "sv_gravity", "showfps", "r_drawviewmodel", "con_notifytime", "cl_alwaysrun"]
    pairs = set()
    for name in common:
        if name not in names:
            continue
        for i in range(len(name)):
            pairs.add((name[:i] + name[i + 1:], name))
            pairs.add((name[:i] + "z" + name[i + 1:], name))
            pairs.add((name[:i] + name[i] + name[i:], name))
            if i + 1 < len(name):
                pairs.add((name[:i] + name[i + 1] + name[i] + name[i + 2:], name))
    # A typo that is itself a valid name must execute normally. Preserve the
    # press/release prefix: a damaged '+'/'-' is intentionally not guessed.
    pairs = sorted((word, name) for word, name in pairs if word not in names
                   and (not name.startswith(("+", "-")) or word[:1] == name[:1]))
    assert pairs
    start = time.monotonic()
    failures = []
    for word, target in pairs:
        out = audit(word)
        if not re.search(r"(?<![\w+-])" + re.escape(target) + r"(?!\w)", out):
            failures.append((word, target, out))
    assert not failures, failures
    for word in ("zzzzzz", "r_zzzzzzzz", "cl_zzzzzzzz", "vid_zzzzzzz", "this_isnt_a_command"):
        assert "Did you mean" not in audit(word), word
    elapsed = time.monotonic() - start
    print(f"Corpus: {len(names)} names; {len(pairs)}/{len(pairs)} generated typos found "
          f"({elapsed * 1000 / len(pairs):.3f} ms/query on this machine)")

    words = ["".join(chars) for length in range(5)
             for chars in itertools.product("ab", repeat=length)]
    checked = 0
    for a, b in itertools.product(words, repeat=2):
        expected = reference_distance(a, b)
        for limit in range(4):
            actual = lib.distance(a.encode(), b.encode(), limit)
            assert actual == expected if expected <= limit else actual > limit, (a, b, limit)
            checked += 1
    print(f"Distance reference: {checked} string/budget combinations passed")
