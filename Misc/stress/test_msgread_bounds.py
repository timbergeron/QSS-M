"""Boundary cases for the MSG_ReadFloat/MSG_ReadDouble guards.

Demos give byte-exact control of net_message.cursize (the frame header is the
length), which injection into a live stream cannot: there the payload is
appended to the server's own traffic, so the truncation is never at the real
end of message.  svc_time is the ideal probe -- every protocol, vanilla
included, parses it with MSG_ReadFloat.
"""
import sys, time, struct
sys.path.insert(0, "/Users/timbergeron/codedev/QSS-M/Misc/stress")
import qssm_stress as S

SVC_TIME = 7

cfg = S.Config(binary=S.find_binary(), paks=S.find_paks(), registered=True,
               results="/tmp/qssm-boundary", scenarios=[], runs=1, minutes=0, seed=0,
               iterations=1, round_robin=False, allow_net=False, nosound=True,
               boot_timeout=90, map_timeout=90, hang_timeout=15, hang_retries=1,
               port_base=27600, rcon_password="x", replay_pace=0.05,
               always_check=True, keep=True)
r = S.Runner(cfg)
eng = r.make_engine("boundary")
d = eng.work / "base" / "id1" / "demos"
d.mkdir(parents=True, exist_ok=True)


def make_demo(name, msg):
    body = b"-1\n" + struct.pack("<i", len(msg)) + struct.pack("<3f", 0, 0, 0) + msg
    (d / (name + ".dem")).write_bytes(body)


# float: svc_time with 0..3 of its 4 float bytes present (and 4 = well formed)
for n in range(0, 5):
    make_demo(f"f{n}", bytes([SVC_TIME]) + b"\x2a" * n)

eng.start()
eng.wait_ready()
print(f"{'case':10s} {'alive':6s} {'overread':9s} verdict")
fails = []
for n in range(0, 5):
    before = len(eng.log_text())
    try:
        eng.send(f"playdemo f{n}", S.waits(25))
        st = eng.status(timeout=12)
        alive = eng.alive()
    except Exception as exc:
        print(f"float-{n:<4d} DIED   -         {exc}")
        fails.append(f"float-{n}")
        break
    new = eng.log_text()[before:]
    over = "STRESS_PARSE" in new
    expect_reject = n < 4            # 0..3 bytes of the float = truncated
    ok = alive and not over
    verdict = "ok" if ok else "FAIL"
    if not ok:
        fails.append(f"float-{n}")
    print(f"float-{n:<4d} {str(alive):6s} {str(over):9s} {verdict}"
          f"{'  (truncated -> must be rejected)' if expect_reject else '  (well formed)'}")
    eng.send("disconnect", S.waits(8))
    eng.status()

print()
print("RESULT:", "all boundary cases handled" if not fails else f"FAILURES: {fails}")
eng.kill()
