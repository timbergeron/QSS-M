"""Console-command stack overflows: long arguments must never crash the client.

MAXCMDLINE is 256, so unbounded sprintf() of a command argument into a
MAXCMDLINE (or smaller) stack buffer overflows on any long line.  Found by the
cmdfuzz lane as SIGTRAP in Host_Say_f2 (fortified sprintf); the same shape
existed in the `st` shortcut and in `color x <long>` (a 14-byte buffer).

Each case is replayed through the harness against QSSM_BIN / --bin; pass means
the replay does not reproduce a crash.  Usage:
    QSSM_BIN=/path/to/stress-enabled/QSS-M python3 test_cmd_overflow.py
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LONG = "A" * 600
CASES = {
    "s":     f"s {LONG}",
    "st":    f"st {LONG}",
    "color": f"color x {LONG}",   # needs a connected client to reach the sprintf
}

def main():
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, cmd in CASES.items():
            journal = os.path.join(tmp, name + ".txt")
            with open(journal, "w") as f:
                f.write("_stress_status 1\nmap start\nwait;wait;wait;wait;wait;wait\n"
                        f"_stress_status 2\n{cmd}\nwait;wait\n_stress_status 3\n")
            out = subprocess.run([sys.executable, os.path.join(HERE, "qssm_stress.py"),
                                  "--replay", journal, "--results", os.path.join(tmp, "r_" + name),
                                  "--nosound"], capture_output=True, text=True)
            last = (out.stdout.strip().splitlines() or ["<no output>"])[-1]
            ok = "reproduced: False" in last
            print(f"{'ok  ' if ok else 'FAIL'} {name:6s} {last}")
            if not ok:
                failures.append(name)
    print("FAILED: " + ", ".join(failures) if failures else "all cases passed")
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())
