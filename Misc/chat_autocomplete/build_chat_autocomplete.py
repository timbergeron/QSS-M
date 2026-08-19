#!/usr/bin/env python3
"""Generate QSS-M's native chat-autocomplete table.

Reads two vendored, human-editable inputs:

  Misc/chat_autocomplete/chat_autocomplete_english.txt   <word> <frequency>
  Misc/chat_autocomplete/chat_autocomplete_curated.txt   <word> <weight-or-tier>

and writes Quake/chat_autocomplete_words.inc.  Both inputs live in the repo, so
regeneration is offline and deterministic; there is no corpus to download and
no network step.

The output is a single NUL-separated string blob plus an index of
{offset, frequency} pairs.  That costs about a sixth of what an array of
{const char *, unsigned} would, and -- because it holds no pointers -- it needs
no load-time relocations at all.

Entries below --min-frequency are dropped: the runtime rejects them before
scoring, so shipping them would only pad the binary.  The threshold is emitted
into the header as CHAT_AUTOCOMPLETE_MIN_FREQUENCY and consumed by keys.c, so
the generator and the engine cannot drift apart.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

WORD_RE = re.compile(r"[a-z]{3,24}\Z")

# Named weight tiers; see Misc/chat_autocomplete/chat_autocomplete_curated.txt for what each one
# is for.  Tuned against the English corpus, whose 99th percentile is ~500.
TIERS = {
    "core": 6000,
    "chat": 900,
    "common": 800,
    "jargon": 60,
}

# Autocomplete is proactive UI, so keep a tiny conservative blocklist even
# though QSS-M still allows users to type any text themselves.
BLOCKED_WORDS = {
    "asshole", "bastard", "bitch", "cocksucker", "cunt", "dick",
    "fag", "faggot", "fuck", "fucked", "fucker", "fucking",
    "motherfucker", "nigger", "nigga", "piss", "pussy", "shit",
    "shitty", "slut", "whore",
}

# MSVC caps a string literal at 65535 bytes; stay clear of it with room to grow.
MAX_BLOB_BYTES = 60000

WORDS_PER_LINE = 8


def read_pairs(path: pathlib.Path, resolve) -> dict[str, int]:
    """Parse a '<word> <value>' file, skipping blank and '#' comment lines."""
    out: dict[str, int] = {}
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2:
            raise SystemExit(f"{path}:{lineno}: expected '<word> <value>', got {raw!r}")
        word, value = parts[0].lower(), parts[1]
        if not WORD_RE.fullmatch(word):
            raise SystemExit(f"{path}:{lineno}: invalid word {word!r}")
        weight = resolve(value, path, lineno)
        if weight <= 0:
            continue
        if word in out:
            raise SystemExit(f"{path}:{lineno}: duplicate word {word!r}")
        out[word] = weight
    return out


def resolve_frequency(value: str, path: pathlib.Path, lineno: int) -> int:
    try:
        return int(value)
    except ValueError:
        raise SystemExit(f"{path}:{lineno}: expected an integer, got {value!r}")


def resolve_weight(value: str, path: pathlib.Path, lineno: int) -> int:
    if value in TIERS:
        return TIERS[value]
    try:
        return int(value)
    except ValueError:
        tiers = ", ".join(sorted(TIERS))
        raise SystemExit(f"{path}:{lineno}: expected an integer or one of {tiers}; got {value!r}")



def suggest(words: list[str], freq: dict[str, int], prefix: str,
            min_prefix: int, dominance: int) -> str | None:
    """Mirror of ChatAuto_Refresh's ranking in keys.c.

    Kept in step by hand; the C harness in the review notes checks them against
    each other.  Used only for the reachability audit below.
    """
    import bisect
    if len(prefix) < min_prefix:
        return None
    best = None
    best_freq = 0
    best_suffix = 0
    second_freq = 0
    exact = False
    for i in range(bisect.bisect_left(words, prefix), len(words)):
        word = words[i]
        if not word.startswith(prefix):
            break
        if len(word) == len(prefix):
            exact = True
            continue
        suffix = len(word) - len(prefix)
        f = freq[word]
        if best is None or f > best_freq or (f == best_freq and suffix < best_suffix):
            if best is not None:
                second_freq = best_freq
            best, best_freq, best_suffix = word, f, suffix
        elif f > second_freq:
            second_freq = f
    if exact or best is None:
        return None
    if second_freq and best_freq * 100 < second_freq * dominance:
        return None
    return best


def audit_curated(words, freq, curated, min_prefix, dominance) -> list[str]:
    """Report curated words that no prefix can reach, and say why.

    Two of the reasons are by design: a stem should beat its own inflections
    ("rocket" over "rockets"), and a prefix that is already a whole word should
    suppress everything ("gib", "run").  The third is a mistake -- two siblings
    left on the same weight cancel each other out under the dominance rule and
    silently produce nothing, which is exactly the bug this audit exists to
    catch.  Only that last category needs action.
    """
    problems = []
    for word in sorted(curated):
        if len(word) <= min_prefix:
            continue	# suppressor only; can never be completed
        prefixes = [word[:n] for n in range(min_prefix, len(word))]
        if any(suggest(words, freq, p, min_prefix, dominance) == word for p in prefixes):
            continue

        rivals = {}
        for p in prefixes:
            if p in freq:
                continue	# the prefix is a word in its own right
            hit = suggest(words, freq, p, min_prefix, dominance)
            rivals[p] = hit
        if not rivals:
            problems.append(f"{word}: every prefix is itself a word")
            continue
        winners = sorted({h for h in rivals.values() if h})
        if winners:
            problems.append(f"{word} (weight {freq[word]}): shadowed by {', '.join(winners)}")
        else:
            tied = sorted({w for p in rivals for w in words
                           if w.startswith(p) and w != p and freq[w] == max(
                               (freq[v] for v in words if v.startswith(p) and v != p),
                               default=0)})
            problems.append(
                f"{word} (weight {freq[word]}): TIE, nothing wins -- "
                f"split the weights of {', '.join(tied[:4])}")
    return problems


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--english", type=pathlib.Path,
                        default=pathlib.Path("Misc/chat_autocomplete/chat_autocomplete_english.txt"))
    parser.add_argument("--curated", type=pathlib.Path,
                        default=pathlib.Path("Misc/chat_autocomplete/chat_autocomplete_curated.txt"))
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("Quake/chat_autocomplete_words.inc"))
    parser.add_argument("--min-frequency", type=int, default=20,
                        help="runtime confidence floor; entries below it are unreachable")
    parser.add_argument("--min-prefix", type=int, default=3,
                        help="must match CHAT_AUTOCOMPLETE_MIN_PREFIX in keys.c")
    parser.add_argument("--dominance-percent", type=int, default=125,
                        help="must match CHAT_AUTOCOMPLETE_DOMINANCE_PERCENT in keys.c")
    args = parser.parse_args()

    english = read_pairs(args.english, resolve_frequency)
    curated = read_pairs(args.curated, resolve_weight)

    # A curated weight replaces the corpus frequency; the corpus cannot know
    # that "quad" matters more than "quality" to someone playing Quake.
    selected = dict(english)
    selected.update(curated)

    for word in BLOCKED_WORDS:
        selected.pop(word, None)

    dropped = {w: f for w, f in selected.items() if f < args.min_frequency}
    selected = {w: f for w, f in selected.items() if f >= args.min_frequency}
    if not selected:
        raise SystemExit("no words survived the frequency filter")

    words = sorted(selected)

    offsets: list[int] = []
    cursor = 0
    for word in words:
        offsets.append(cursor)
        cursor += len(word) + 1
    blob_bytes = cursor
    if blob_bytes > MAX_BLOB_BYTES:
        raise SystemExit(
            f"blob is {blob_bytes} bytes, over the {MAX_BLOB_BYTES}-byte cap; "
            "raise --min-frequency or trim the inputs")

    lines: list[str] = []
    lines.append("/* Generated by Misc/chat_autocomplete/build_chat_autocomplete.py -- do not edit by hand. */")
    lines.append("/* Regenerate with: python3 Misc/chat_autocomplete/build_chat_autocomplete.py */")
    lines.append("/*")
    lines.append(f" * {len(words)} words, sorted lexicographically for binary prefix lookup.")
    lines.append(f" * Blob {blob_bytes} bytes + index {len(words) * 8} bytes, no relocations.")
    lines.append(" */")
    lines.append("")
    lines.append(f"#define CHAT_AUTOCOMPLETE_MIN_FREQUENCY {args.min_frequency}u")
    lines.append("")
    lines.append("static const char chat_autocomplete_blob[] =")
    for i in range(0, len(words), WORDS_PER_LINE):
        chunk = words[i:i + WORDS_PER_LINE]
        lines.append('\t"' + "".join(w + r"\0" for w in chunk) + '"')
    lines.append("\t;")
    lines.append("")
    lines.append("static const chat_autocomplete_word_t chat_autocomplete_words[] =")
    lines.append("{")
    for word, offset in zip(words, offsets):
        lines.append(f"\t{{ {offset}u, {selected[word]}u }},")
    lines.append("};")
    lines.append("")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8", newline="\n")

    sys.stderr.write(
        f"{args.output}: {len(words)} words, blob {blob_bytes} B, "
        f"index {len(words) * 8} B, total {(blob_bytes + len(words) * 8) / 1024:.1f} KiB\n")
    sys.stderr.write(
        f"  english {len(english)}, curated {len(curated)}, "
        f"dropped below frequency {args.min_frequency}: {len(dropped)}\n")

    problems = audit_curated(words, selected, curated,
                             args.min_prefix, args.dominance_percent)
    if problems:
        sys.stderr.write(f"  {len(problems)} curated words no prefix reaches:\n")
        for line in problems:
            sys.stderr.write(f"    {line}\n")


if __name__ == "__main__":
    main()
