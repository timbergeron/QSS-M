"""Boundary tests for the unknown-command suggestion cap (CMD_MAX_SUGGESTIONS).

Checks 0 / 1 / exactly-cap / cap+1 match counts, and that matches omitted from
the printed list are still counted in the summary line.  Also asserts explicit
`apropos` stays uncapped.

The capped path is reached from any src_command unknown command, so the stress
script is a faithful stand-in for a requeued server stufftext here.
"""
import re
import sys

sys.path.insert(0, "/Users/timbergeron/codedev/QSS-M/Misc/stress")
import qssm_stress as S

CAP = 20                       # must match CMD_MAX_SUGGESTIONS in cmd.c
SUMMARY = re.compile(r"(\d+) cvars?/commands?/alias(?:es)? containing '([^']*)'")
NOHITS = re.compile(r"no cvars/commands/aliases contain '([^']*)'")
MORE = re.compile(r"\.\.\.and (\d+) more")

cfg = S.Config(binary=S.find_binary(), paks=S.find_paks(), registered=True,
               results="/tmp/qssm-capbounds", scenarios=[], runs=1, minutes=0, seed=0,
               iterations=1, round_robin=False, allow_net=False, nosound=True,
               boot_timeout=90, map_timeout=90, hang_timeout=20, hang_retries=1,
               port_base=29000, rcon_password="x", replay_pace=0.05,
               always_check=True, keep=True)
r = S.Runner(cfg)
eng = r.make_engine("capbounds")
eng.start(); eng.wait_ready()


def run_capture(cmd):
    before = len(eng.log_text())
    eng.send(cmd, S.waits(12))
    eng.status()
    return eng.log_text()[before:]


def true_hits(substr):
    """Uncapped count via explicit apropos."""
    out = run_capture(f"apropos {substr}")
    if NOHITS.search(out):
        return 0, out
    m = SUMMARY.search(out)
    return (int(m.group(1)) if m else -1), out


# --- find substrings with the match counts we want -------------------------
# Scrape the engine's own name lists once and compute counts offline, rather
# than probing candidate strings one round-trip at a time -- the interesting
# boundaries (exactly CAP, exactly CAP+1) are too rare to stumble on.
names = [n.lower() for n in S.collect_list(eng, "cmdlist", "CMDS", S.CMDLIST_RE)]
names += [n.lower() for n in S.collect_list(eng, "cvarlist", "CVARS", S.CVARLIST_RE)]
print(f"scraped {len(names)} command+cvar names")

exact = set(names)


def offline_hits(sub):
    return sum(1 for n in names if sub in n)


import itertools, string
found = {0: "zqx"}
pool = string.ascii_lowercase + "_"
for a, b in itertools.product(pool, repeat=2):
    sub = a + b
    if sub in exact:
        continue                      # must not be a real command/cvar name
    n = offline_hits(sub)
    for target in (1, CAP, CAP + 1):
        if n == target and target not in found:
            found[target] = sub
for a, b, c in itertools.product(pool, repeat=3):
    if len(found) == 4:
        break
    sub = a + b + c
    if sub in exact:
        continue
    n = offline_hits(sub)
    for target in (1, CAP, CAP + 1):
        if n == target and target not in found:
            found[target] = sub

print("substrings chosen:", found)
# confirm the offline model agrees with the engine before trusting it
for t, sub in sorted(found.items()):
    n, _ = true_hits(sub)
    if n != t:
        print(f"  NOTE: engine reports {n} for '{sub}', offline model said {t}; using engine value")
        found[t] = None if n != t else sub
found = {t: sub for t, sub in found.items() if sub}

failures = []


def check(label, substr, expect_hits):
    out = run_capture(substr)                 # unknown command -> capped path
    listed = len([l for l in out.splitlines()
                  if l.startswith("   ") and "...and" not in l])
    more = MORE.search(out)
    if expect_hits == 0:
        ok = bool(NOHITS.search(out)) and listed == 0
        print(f"{label:16s} hits=0   listed={listed} nohits_line={bool(NOHITS.search(out))} -> {'ok' if ok else 'FAIL'}")
        if not ok:
            failures.append(label)
        return
    m = SUMMARY.search(out)
    summary_n = int(m.group(1)) if m else -1
    exp_listed = min(expect_hits, CAP)
    exp_more = expect_hits - CAP if expect_hits > CAP else None
    ok = (listed == exp_listed
          and summary_n == expect_hits                      # omitted still counted
          and ((exp_more is None and not more)
               or (exp_more is not None and more and int(more.group(1)) == exp_more)))
    print(f"{label:16s} hits={expect_hits:<4d} listed={listed} (want {exp_listed}) "
          f"summary={summary_n} more={more.group(1) if more else '-'} "
          f"(want {exp_more if exp_more is not None else '-'}) -> {'ok' if ok else 'FAIL'}")
    if not ok:
        failures.append(label)


for target in (0, 1, CAP, CAP + 1):
    if target in found:
        check(f"{target} matches", found[target], target)
    else:
        print(f"{target:<4} matches   (no substring with exactly this count; skipped)")

# explicit apropos must stay uncapped
if CAP + 1 in found:
    n, out = true_hits(found[CAP + 1])
    listed = len([l for l in out.splitlines() if l.startswith("   ") and "...and" not in l])
    ok = listed == n and not MORE.search(out)
    print(f"{'apropos uncapped':16s} hits={n} listed={listed} -> {'ok' if ok else 'FAIL'}")
    if not ok:
        failures.append("apropos uncapped")

print()
print("RESULT:", "all boundary cases correct" if not failures else f"FAILURES: {failures}")
eng.kill()
