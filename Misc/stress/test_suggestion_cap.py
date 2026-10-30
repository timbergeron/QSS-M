"""Boundary tests for the unknown-command suggestion cap (CMD_MAX_SUGGESTIONS).

Checks 0 / 1 / exactly-cap / cap+1 match counts, and that matches omitted from
the printed list are still counted in the summary line.  Also asserts explicit
`apropos` stays uncapped.

The capped path handles src_command prefix searches, so the stress script is
a faithful stand-in for a requeued server stufftext here.
"""
import re
import qssm_stress as S

CAP = 20                       # must match CMD_MAX_SUGGESTIONS in cmd.c
SUMMARY = re.compile(r"(\d+) cvars?/commands?/alias(?:es)? containing '([^']*)'")
NOHITS = re.compile(r"no cvars/commands/aliases contain '([^']*)'")
MORE = re.compile(r"\.\.\.and (\d+) more")
UNKNOWN = re.compile(r'Unknown command "([^"]*)"')

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


# Deliberate prefixes always exercise substring discovery; arbitrary fragments
# may now be spelling corrections ("connectt" -> "connect"). Give every cap
# boundary a predictable fixture, independent of the engine's current names.
found = {0: "zzcap_no_matches_zqx"}
for count in (1, CAP, CAP + 1):
    prefix = f"zzcap{count}_"
    found[count] = prefix
    for i in range(count):
        eng.send(f'alias {prefix}{i:02d} "echo cap_fixture"')
eng.status()
for count, prefix in found.items():
    actual, _ = true_hits(prefix)
    assert actual == count, (prefix, actual, count)

failures = []


def check(label, substr, expect_hits):
    out = run_capture(substr)                 # unknown command -> capped path
    listed = len([l for l in out.splitlines()
                  if l.startswith("   ") and "...and" not in l])
    more = MORE.search(out)
    if expect_hits == 0:
        ok = bool(UNKNOWN.search(out)) and listed == 0
        print(f"{label:16s} hits=0   listed={listed} unknown_line={bool(UNKNOWN.search(out))} -> {'ok' if ok else 'FAIL'}")
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
