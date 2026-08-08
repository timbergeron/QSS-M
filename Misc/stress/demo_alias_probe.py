#!/usr/bin/env python3
"""Visible, repeatable alias-instancing A/B test over recorded demos."""

import argparse
import json
import statistics
import sys
from pathlib import Path

from live_client_probe import LiveClient

SUITE_PATH = Path(__file__).with_name("alias_instancing_demos.json")


def suite_demos(name):
    suites = json.loads(SUITE_PATH.read_text(encoding="utf-8"))
    entries = suites["demos"]
    if name == "best-data":
        entries = [entry for entry in entries if entry["tier"] == "primary"]
    return [entry["demo"] for entry in entries]


def demo_stem(value):
    return Path(value).stem


def run(args):
    output = Path(args.output).resolve()
    client = LiveClient(args.bin, args.basedir, output, args.width, args.height)
    demos = args.demo or suite_demos(args.suite)
    report = {
        "binary": str(Path(args.bin).resolve()),
        "basedir": str(Path(args.basedir).resolve()),
        "resolution": [args.width, args.height],
        "seek": args.seek,
        "sample_seconds": args.sample_seconds,
        "repeats": args.repeats,
        "suite": args.suite if not args.demo else None,
        "demos": demos,
        "raw": [],
        "errors": [],
    }
    try:
        client.start()
        client.send(
            "host_maxfps 0", "vid_vsync 0", "r_speeds 0",
            "r_outline 0", "r_drawentities 1",
        )
        client.barrier()

        for rep in range(args.repeats):
            # Alternate order each repeat so startup and thermal drift do not
            # consistently favor one side of the comparison.
            variants = ((0, "off"), (1, "on"))
            if rep & 1:
                variants = tuple(reversed(variants))
            for demo_arg in demos:
                demo = demo_stem(demo_arg)
                for enabled, variant in variants:
                    try:
                        client.send(
                            "disconnect",
                            "gl_alias_instancing {}".format(enabled),
                            "playdemo \"{}\"".format(demo),
                        )
                        client.wait_spawned(args.demo_timeout)
                        if args.seek:
                            client.send("jumpdemo {}".format(args.seek))
                            client.barrier(args.demo_timeout)
                            client.wait_spawned(args.demo_timeout)
                        # Demos may contain stufftext that changes client cvars.
                        # Reassert every benchmark control after playback and
                        # seeking have reached the sampled state.
                        client.send(
                            "host_maxfps 0", "vid_vsync 0", "r_speeds 0",
                            "r_outline 0", "r_drawentities 1",
                            "gl_alias_instancing {}".format(enabled),
                        )
                        client.barrier()
                        client.settle(args.warmup)
                        label = "alias_{}_{}_{}".format(variant, demo, rep)
                        sample = client.profile(label, args.sample_seconds)
                        sample.update({
                            "demo": demo, "variant": variant, "repeat": rep,
                        })
                        report["raw"].append(sample)
                        print(
                            "{:<44} fps={:<9} p50={:<8} p90={:<8} p99={:<8}".format(
                                label, sample.get("fps", "-"), sample.get("p50", "-"),
                                sample.get("p90", "-"), sample.get("p99", "-")),
                            flush=True,
                        )
                    except TimeoutError as exc:
                        error = {"demo": demo, "variant": variant, "repeat": rep,
                                 "error": str(exc)}
                        report["errors"].append(error)
                        print("SKIP {} {} repeat {}: {}".format(
                            demo, variant, rep, exc), flush=True)
                        client.send("disconnect")
                        client.barrier()
    finally:
        client.stop()

    # Repeat zero is warm-up when enough repeats are requested.
    usable = [s for s in report["raw"] if args.repeats < 3 or s["repeat"] > 0]
    summary = {}
    for demo in {s["demo"] for s in usable}:
        cells = {}
        for variant in ("off", "on"):
            samples = [s for s in usable if s["demo"] == demo and s["variant"] == variant]
            cells[variant] = {
                key: round(statistics.median(s[key] for s in samples if key in s), 3)
                for key in ("fps", "p50", "p90", "p99")
                if any(key in s for s in samples)
            }
        if cells["off"].get("fps"):
            cells["gain_percent"] = round(
                (cells["on"].get("fps", 0) / cells["off"]["fps"] - 1) * 100, 3)
        summary[demo] = cells
    report["summary"] = summary
    output.mkdir(parents=True, exist_ok=True)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", required=True)
    parser.add_argument("--basedir", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--demo", action="append",
                        help="demo stem or filename; overrides --suite")
    parser.add_argument("--suite", choices=("best-data", "extended"), default="best-data",
                        help="curated demo suite used when --demo is omitted")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--seek", default="2:00")
    parser.add_argument("--warmup", type=float, default=3)
    parser.add_argument("--sample-seconds", type=float, default=10)
    parser.add_argument("--repeats", type=int, default=4)
    parser.add_argument("--demo-timeout", type=float, default=180)
    args = parser.parse_args(argv)
    try:
        report = run(args)
    except (OSError, RuntimeError, TimeoutError) as exc:
        print("demo alias probe failed: {}".format(exc), file=sys.stderr)
        return 1
    print(json.dumps(report["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
