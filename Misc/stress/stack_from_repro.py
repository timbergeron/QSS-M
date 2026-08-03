"""Get a crash stack for a finding when macOS has throttled .ips generation.

After enough crashes macOS stops writing DiagnosticReports, so the harness sees
"SIGSEGV (no report)" with no frames.  Re-run the finding's sandbox under lldb
instead: same binary, same sandbox, same asset, and lldb prints the backtrace
regardless of ReportCrash.

    python3 stack_from_repro.py <finding-dir> [map]
"""
import re
import subprocess
import sys
from pathlib import Path

d = Path(sys.argv[1])
base = d / "base"
if not base.is_dir():
    sys.exit(f"no sandbox at {base}")

# the asset the run was on when it died, from the journal
journal = (d / "journal.txt").read_text(errors="replace").splitlines()
assets = [l for l in journal if l.startswith("// asset ")]
last = assets[-1] if assets else "(unknown)"
print(f"last asset: {last}")

# the map to load: prefer the last "map X" the journal issued
maps = [l.split()[1] for l in journal if re.match(r"^map \S+$", l.strip())]
mapname = sys.argv[2] if len(sys.argv) > 2 else (maps[-1] if maps else "start")
print(f"loading map: {mapname}")

binary = subprocess.run(
    ["bash", "-lc",
     "ls ~/Library/Developer/Xcode/DerivedData/QuakeSpasm-*/Build/Products/Debug/"
     "QSS-M.app/Contents/MacOS/QSS-M | head -1"],
    capture_output=True, text=True).stdout.strip()

script = d / "lldb.cmds"
script.write_text(
    f"settings set -- target.run-args -basedir {base} -window -width 320 -height 240 "
    f"-nosound -ApplePersistenceIgnoreState YES +map {mapname}\n"
    "run\n"
    "bt 25\n"
    "quit\n")

print("running under lldb (may take ~30s)...")
out = subprocess.run(["lldb", "-b", "-s", str(script), binary],
                     capture_output=True, text=True, timeout=300)
text = out.stdout + out.stderr

stop = [l for l in text.splitlines() if "stop reason" in l]
frames = [l.strip() for l in text.splitlines() if re.match(r"^\s*(\*?\s*)frame #", l)]
print()
print("stop reason:", stop[0].strip() if stop else "(none - did not crash)")
for f in frames[:14]:
    print("   ", f[:120])
if not frames:
    print(text[-1500:])
