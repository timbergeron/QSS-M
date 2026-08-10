#!/usr/bin/env python3
"""Deterministic visual A/B probe for alias-model instancing.

The probe records one live start-map scene, then replays that exact network
stream with alias instancing disabled and enabled.  Demo playback is paused at
matching timestamps before each screenshot, so gameplay timing cannot make an
off/on pair drift apart.  A normalized crop keeps the artifacts small and
focuses the comparison on the wall zombies involved in the lava-ball
regression.

By default this is a report-only probe: it writes paired crops, amplified diff
images, and visual-report.json.  Pass --max-changed-fraction after calibrating
a known-good build to turn the metric into a failing regression oracle.
"""

import argparse
import hashlib
import json
import random
import re
import secrets
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qssm_stress as S
import rcon_probe as R


DEFAULT_POSITION = (700, 700, 50, 0, 90)
DEFAULT_CROP = (0.70, 0.30, 0.18, 0.30)
VISUAL_CONTROLS = (
    "host_maxfps 0",
    "vid_vsync 0",
    "r_outline 0",
    "r_drawentities 1",
    "r_drawviewmodel 0",
    "r_particles 0",
    "r_dynamic 0",
    "crosshair 0",
    "scr_showfps 0",
    "scr_clock 0",
    "scr_showpause 0",
    "scr_demobar_timeout -1",
    "con_notifytime 0",
    "viewsize 120",
)


class VisualProbeError(Exception):
    """A visual fixture or image oracle could not be completed."""


def parse_csv_numbers(value, count, label):
    try:
        values = tuple(float(part.strip()) for part in value.split(","))
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{label} must contain numbers") from exc
    if len(values) != count:
        raise argparse.ArgumentTypeError(
            f"{label} requires {count} comma-separated values")
    return values


def parse_position(value):
    return parse_csv_numbers(value, 5, "position")


def parse_crop(value):
    if value.strip().lower() == "full":
        return None
    crop = parse_csv_numbers(value, 4, "crop")
    x, y, width, height = crop
    if (x < 0 or y < 0 or width <= 0 or height <= 0
            or x + width > 1 or y + height > 1):
        raise argparse.ArgumentTypeError(
            "crop must be normalized x,y,width,height inside 0..1")
    return crop


def sample_times(start, end, step):
    if start < 0 or end < start or step <= 0:
        raise ValueError("sample times require 0 <= start <= end and step > 0")
    result = []
    value = start
    # Integer stepping avoids cumulative rounding changing the final sample.
    index = 0
    while value <= end + step * 1e-6:
        result.append(round(value, 6))
        index += 1
        value = start + index * step
    return result


def read_tga(path):
    """Read the uncompressed 24-bit TGA emitted by QSS-M screenshots."""
    data = Path(path).read_bytes()
    if len(data) < 18:
        raise VisualProbeError(f"truncated TGA header: {path}")
    id_length, color_map_type, image_type = data[0], data[1], data[2]
    width = data[12] | (data[13] << 8)
    height = data[14] | (data[15] << 8)
    bpp = data[16]
    descriptor = data[17]
    if image_type != 2 or color_map_type != 0 or bpp != 24:
        raise VisualProbeError(
            f"unsupported TGA {path}: type={image_type} cmap={color_map_type} "
            f"bpp={bpp}")
    offset = 18 + id_length
    source = data[offset:offset + width * height * 3]
    if width <= 0 or height <= 0 or len(source) != width * height * 3:
        raise VisualProbeError(f"truncated TGA pixels: {path}")

    top_down = bool(descriptor & 0x20)
    pixels = bytearray(width * height * 3)
    for y in range(height):
        source_y = y if top_down else height - 1 - y
        source_row = source_y * width * 3
        target_row = y * width * 3
        for x in range(width):
            source_pixel = source_row + x * 3
            target_pixel = target_row + x * 3
            pixels[target_pixel:target_pixel + 3] = (
                source[source_pixel + 2],
                source[source_pixel + 1],
                source[source_pixel],
            )
    return {"width": width, "height": height, "pixels": bytes(pixels)}


def write_tga(path, image):
    """Write a top-down uncompressed 24-bit TGA from packed RGB pixels."""
    width, height, pixels = image["width"], image["height"], image["pixels"]
    if len(pixels) != width * height * 3:
        raise ValueError("image pixel buffer has the wrong size")
    header = bytearray(18)
    header[2] = 2
    header[12] = width & 0xff
    header[13] = (width >> 8) & 0xff
    header[14] = height & 0xff
    header[15] = (height >> 8) & 0xff
    header[16] = 24
    header[17] = 0x20
    bgr = bytearray(len(pixels))
    for offset in range(0, len(pixels), 3):
        bgr[offset:offset + 3] = (
            pixels[offset + 2], pixels[offset + 1], pixels[offset])
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(header) + bytes(bgr))


def crop_image(image, normalized_crop):
    if normalized_crop is None:
        return dict(image), (0, 0, image["width"], image["height"])
    width, height = image["width"], image["height"]
    x, y, crop_width, crop_height = normalized_crop
    x0 = min(width - 1, max(0, int(round(x * width))))
    y0 = min(height - 1, max(0, int(round(y * height))))
    x1 = min(width, max(x0 + 1, int(round((x + crop_width) * width))))
    y1 = min(height, max(y0 + 1, int(round((y + crop_height) * height))))
    out_width, out_height = x1 - x0, y1 - y0
    source = image["pixels"]
    output = bytearray(out_width * out_height * 3)
    for row in range(out_height):
        source_start = ((y0 + row) * width + x0) * 3
        target_start = row * out_width * 3
        output[target_start:target_start + out_width * 3] = \
            source[source_start:source_start + out_width * 3]
    return ({"width": out_width, "height": out_height,
             "pixels": bytes(output)},
            (x0, y0, out_width, out_height))


def compare_images(reference, candidate, pixel_tolerance=2, diff_gain=4):
    if (reference["width"], reference["height"]) != \
            (candidate["width"], candidate["height"]):
        raise VisualProbeError("paired image dimensions differ")
    left, right = reference["pixels"], candidate["pixels"]
    total_pixels = reference["width"] * reference["height"]
    total_abs = 0
    max_abs = 0
    changed_pixels = 0
    diff = bytearray(len(left))
    for offset in range(0, len(left), 3):
        pixel_max = 0
        for channel in range(3):
            delta = abs(left[offset + channel] - right[offset + channel])
            total_abs += delta
            max_abs = max(max_abs, delta)
            pixel_max = max(pixel_max, delta)
            diff[offset + channel] = min(255, delta * diff_gain)
        if pixel_max > pixel_tolerance:
            changed_pixels += 1
    metrics = {
        "max_abs": max_abs,
        "mean_abs": total_abs / float(total_pixels * 3),
        "changed_pixels": changed_pixels,
        "total_pixels": total_pixels,
        "changed_fraction": changed_pixels / float(total_pixels),
        "pixel_tolerance": pixel_tolerance,
        "diff_gain": diff_gain,
        "reference_sha256": hashlib.sha256(left).hexdigest(),
        "candidate_sha256": hashlib.sha256(right).hexdigest(),
    }
    return metrics, {
        "width": reference["width"], "height": reference["height"],
        "pixels": bytes(diff),
    }


def capture_crop(eng, cfg, label, normalized_crop):
    source_dir = eng.work / "base" / "id1" / "screenshots"
    before = {path: (path.stat().st_mtime_ns, path.stat().st_size)
              for path in source_dir.glob("*.tga")}
    eng.send("screenshot tga 90")
    eng.status(timeout=min(cfg.hang_timeout, 10.0))
    changed = [path for path in source_dir.glob("*.tga")
               if before.get(path) != (path.stat().st_mtime_ns,
                                       path.stat().st_size)]
    if not changed:
        raise VisualProbeError(f"{label}: screenshot command produced no TGA")
    source = max(changed, key=lambda path: path.stat().st_mtime_ns)
    image = read_tga(source)
    cropped, crop_pixels = crop_image(image, normalized_crop)
    artifact_dir = eng.work / "visuals"
    destination = artifact_dir / (re.sub(r"[^A-Za-z0-9_.-]+", "-", label)
                                  + ".tga")
    write_tga(destination, cropped)
    try:
        source.unlink()
    except OSError:
        pass
    if eng.event_log:
        eng.event_log.record(
            "artifact", actor=eng.actor, kind="visual-crop", label=label,
            path=str(destination.relative_to(eng.work)), crop=list(crop_pixels))
    print(f"    crop -> {destination}", flush=True)
    return cropped, destination, crop_pixels


def make_config(args, results):
    paks = S.find_paks(args.paks)
    return S.Config(
        binary=S.find_binary(args.binary),
        paks=paks,
        loose_root=S.find_loose_root(args.loose_root, paks),
        registered=any(path.name == "pak1.pak" for path in paks),
        results=str(results),
        scenarios=["alias-visual"],
        runs=1,
        minutes=0,
        seed=args.seed,
        iterations=1,
        round_robin=False,
        allow_net=False,
        remote=None,
        nosound=not args.sound,
        contain=args.contain,
        cpu_limit=args.cpu_limit,
        memory_limit_mb=args.memory_limit_mb,
        file_limit_mb=args.file_limit_mb,
        fd_limit=args.fd_limit,
        process_limit=args.process_limit,
        boot_timeout=args.boot_timeout,
        map_timeout=args.map_timeout,
        hang_timeout=args.hang_timeout,
        hang_retries=args.hang_retries,
        port_base=(args.port_base if args.fixed_local else
                   args.port_base + secrets.randbelow(2048)),
        rcon_password=args.rcon_password or secrets.token_hex(16),
        replay_pace=0.05,
        always_check=True,
        keep=args.keep,
        position_tolerance=args.position_tolerance,
    )


def require_demo_spawned(eng, cfg, label):
    state = eng.wait_spawned(timeout=cfg.map_timeout)
    if state.get("state") != "2" or state.get("signon") != "4":
        raise VisualProbeError(
            f"{label}: demo did not reach signon 4: {state}")
    return state


def record_fixture(eng, cfg, args, reply_log, demo_name):
    eng.send("listen 1", f"map {args.map_name}")
    R.require_fully_spawned(eng, cfg, "alias-visual-record")
    R.run_rcon(eng, "god", reply_log)
    R.setpos_verified(eng, args.position, reply_log, cfg)
    eng.send(*VISUAL_CONTROLS, f"record {demo_name}")
    eng.status(timeout=min(cfg.hang_timeout, 10.0))
    eng.settle(args.record_seconds)
    eng.send("stop")
    eng.status(timeout=min(cfg.hang_timeout, 10.0))

    demo = eng.work / "base" / "id1" / "demos" / f"{demo_name}.dem"
    if not demo.exists() or demo.stat().st_size <= 0:
        raise VisualProbeError("recorded visual fixture was not written")
    artifact = eng.work / "visuals" / demo.name
    artifact.parent.mkdir(exist_ok=True)
    shutil.copy2(demo, artifact)
    if eng.event_log:
        eng.event_log.record(
            "artifact", actor=eng.actor, kind="demo-fixture",
            path=str(artifact.relative_to(eng.work)), size=artifact.stat().st_size)
    return demo, artifact


def capture_variant(eng, cfg, args, demo_name, alpha_sort, instancing,
                    times, captures):
    variant = "on" if instancing else "off"
    eng.send("disconnect")
    eng.status(timeout=min(cfg.hang_timeout, 10.0))
    eng.send(*VISUAL_CONTROLS,
             f"r_alphasort {alpha_sort}",
             f"gl_alias_instancing {instancing}",
             f"playdemo {demo_name}")
    require_demo_spawned(eng, cfg, f"alphasort-{alpha_sort}/{variant}")
    # Demos can contain stufftext which changes client cvars. Reassert after
    # signon, then pause before seeking; QSS-M's seek pump still runs while a
    # demo is paused and renders the landed frame once seeking completes.
    eng.send(*VISUAL_CONTROLS,
             f"r_alphasort {alpha_sort}",
             f"gl_alias_instancing {instancing}",
             "pause")
    eng.status(timeout=min(cfg.hang_timeout, 10.0))
    eng.settle(args.pause_settle)

    for sample_time in times:
        eng.send(f"jumpdemo {sample_time:g}s")
        eng.settle(args.seek_settle)
        time_label = f"{sample_time:06.2f}".replace(".", "p")
        label = f"alphasort-{alpha_sort}-instancing-{variant}-time-{time_label}"
        image, path, crop_pixels = capture_crop(eng, cfg, label, args.crop)
        captures[(alpha_sort, instancing, sample_time)] = {
            "image": image,
            "path": path,
            "crop_pixels": crop_pixels,
        }


def compare_captures(eng, args, times, captures):
    comparisons = []
    failures = []
    for alpha_sort in args.alpha_sort:
        for sample_time in times:
            reference = captures[(alpha_sort, 0, sample_time)]
            candidate = captures[(alpha_sort, 1, sample_time)]
            metrics, diff = compare_images(
                reference["image"], candidate["image"],
                pixel_tolerance=args.pixel_tolerance,
                diff_gain=args.diff_gain)
            time_label = f"{sample_time:06.2f}".replace(".", "p")
            diff_path = (eng.work / "visuals" /
                         f"diff-alphasort-{alpha_sort}-time-{time_label}.tga")
            write_tga(diff_path, diff)
            entry = {
                "r_alphasort": alpha_sort,
                "time": sample_time,
                "instancing_off": str(reference["path"].relative_to(eng.work)),
                "instancing_on": str(candidate["path"].relative_to(eng.work)),
                "diff": str(diff_path.relative_to(eng.work)),
                "crop_pixels": list(reference["crop_pixels"]),
                **metrics,
            }
            comparisons.append(entry)
            if (args.max_changed_fraction is not None and
                    metrics["changed_fraction"] > args.max_changed_fraction):
                failures.append(
                    f"r_alphasort={alpha_sort} time={sample_time:g}s "
                    f"changed_fraction={metrics['changed_fraction']:.6f} > "
                    f"{args.max_changed_fraction:.6f}")
            if (args.max_mean_diff is not None and
                    metrics["mean_abs"] > args.max_mean_diff):
                failures.append(
                    f"r_alphasort={alpha_sort} time={sample_time:g}s "
                    f"mean_abs={metrics['mean_abs']:.6f} > "
                    f"{args.max_mean_diff:.6f}")
    return comparisons, failures


def run_probe(runner, cfg, args, seed):
    eng = runner.make_engine("alias-visual")
    runner.active_event_log = S.EventLog(eng.work / "events.jsonl")
    eng.event_log = runner.active_event_log
    reply_log = eng.work / "rcon-replies.log"
    demo_name = "alias_visual_lavaball"
    times = sample_times(args.start_time, args.end_time, args.step)
    captures = {}
    comparisons = []
    failures = []
    cause = None
    finding = None
    fixture_artifact = None

    try:
        print(f"[run {runner.run_index:04d}] alias-visual seed={seed} "
              f"map={args.map_name} port={eng.port}", flush=True)
        eng.start()
        eng.wait_ready(timeout=cfg.boot_timeout)
        _, fixture_artifact = record_fixture(
            eng, cfg, args, reply_log, demo_name)
        for alpha_sort in args.alpha_sort:
            for instancing in (0, 1):
                capture_variant(eng, cfg, args, demo_name, alpha_sort,
                                instancing, times, captures)
        comparisons, failures = compare_captures(eng, args, times, captures)
        if failures:
            eng.assertion_failure = "alias visual mismatch: " + failures[0]
            eng.journal.append("// ASSERTION FAILED: " + eng.assertion_failure)
    except S.Dead as exc:
        cause = f"died: {exc}"
    except S.Hung as exc:
        cause = f"hang: {exc}"
        eng.sample()
    except (R.ProbeFailure, VisualProbeError) as exc:
        cause = f"assertion: {exc}"
        eng.assertion_failure = str(exc)
    except Exception as exc:
        cause = f"harness error: {exc!r}"
        eng.assertion_failure = cause
    finally:
        report = {
            "schema": 1,
            "binary": getattr(cfg, "binary_provenance", {}),
            "fixture": {
                "map": args.map_name,
                "position": list(args.position),
                "record_seconds": args.record_seconds,
                "demo": (str(fixture_artifact.relative_to(eng.work))
                         if fixture_artifact else None),
            },
            "controls": list(VISUAL_CONTROLS),
            "r_alphasort": list(args.alpha_sort),
            "times": times,
            "crop_normalized": list(args.crop) if args.crop else None,
            "pixel_tolerance": args.pixel_tolerance,
            "limits": {
                "max_changed_fraction": args.max_changed_fraction,
                "max_mean_diff": args.max_mean_diff,
            },
            "comparisons": comparisons,
            "failures": failures,
            "result": ("fail" if failures or eng.assertion_failure else
                       "report-only" if args.max_changed_fraction is None and
                       args.max_mean_diff is None else "pass"),
        }
        (eng.work / "visual-report.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n")
        if cause is None:
            eng.graceful_quit()
            if eng.alive():
                cause = "hang: engine did not exit after quit"
        (eng.work / "journal.txt").write_text("\n".join(eng.journal) + "\n")
        finding = S.classify_exit(
            eng, runner.crashwatch, "alias-visual", seed, cause or "clean")
        eng.kill()
        eng.read_log()
        runner.active_event_log = None

    if finding:
        runner.record(finding, eng)
    elif cause:
        print(f"    (unclassified: {cause})", flush=True)
    else:
        print(f"    {report['result']}: {len(comparisons)} comparisons", flush=True)
    runner.runs_done += 1
    return finding, eng.work / "visual-report.json"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", dest="binary", help="path to QSS-M executable")
    parser.add_argument("--paks", help="directory holding pak0.pak/pak1.pak")
    parser.add_argument("--loose-root", help="game root containing loose assets")
    parser.add_argument(
        "--results", default=str(Path(__file__).parent / "tmp" / "alias-visual"))
    parser.add_argument("--map", dest="map_name", default="start")
    parser.add_argument(
        "--position", type=parse_position,
        default=DEFAULT_POSITION, metavar="X,Y,Z,PITCH,YAW")
    parser.add_argument("--position-tolerance", type=float, default=1.0)
    parser.add_argument("--record-seconds", type=float, default=12.0)
    parser.add_argument("--start-time", type=float, default=1.0)
    parser.add_argument("--end-time", type=float, default=11.0)
    parser.add_argument("--step", type=float, default=0.25)
    parser.add_argument(
        "--crop", type=parse_crop, default=DEFAULT_CROP,
        metavar="X,Y,W,H", help="normalized crop, or 'full'")
    parser.add_argument(
        "--alpha-sort", type=int, choices=(0, 1), action="append",
        help="r_alphasort value; repeat to select both (default: both)")
    parser.add_argument("--pixel-tolerance", type=int, default=2)
    parser.add_argument("--diff-gain", type=int, default=4)
    parser.add_argument(
        "--max-changed-fraction", type=float,
        help="fail if any paired crop exceeds this changed-pixel fraction")
    parser.add_argument(
        "--max-mean-diff", type=float,
        help="fail if any paired crop exceeds this mean channel difference")
    parser.add_argument("--pause-settle", type=float, default=0.15)
    parser.add_argument("--seek-settle", type=float, default=0.15)
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--rcon-password")
    parser.add_argument("--port-base", type=int, default=28600)
    parser.add_argument("--fixed-local", action="store_true")
    parser.add_argument("--boot-timeout", type=float, default=90)
    parser.add_argument("--map-timeout", type=float, default=90)
    parser.add_argument("--hang-timeout", type=float, default=20)
    parser.add_argument("--hang-retries", type=int, default=2)
    parser.add_argument("--sound", action="store_true")
    parser.add_argument("--contain", action="store_true")
    parser.add_argument("--cpu-limit", type=int, default=300)
    parser.add_argument("--memory-limit-mb", type=int, default=4096)
    parser.add_argument("--file-limit-mb", type=int, default=256)
    parser.add_argument("--fd-limit", type=int, default=256)
    parser.add_argument("--process-limit", type=int, default=0)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args(argv)

    if args.alpha_sort is None:
        args.alpha_sort = [0, 1]
    else:
        args.alpha_sort = list(dict.fromkeys(args.alpha_sort))
    if args.position_tolerance < 0:
        parser.error("--position-tolerance must be non-negative")
    if args.record_seconds <= 0:
        parser.error("--record-seconds must be positive")
    if args.end_time > args.record_seconds:
        parser.error("--end-time must not exceed --record-seconds")
    if args.pixel_tolerance < 0 or args.pixel_tolerance > 255:
        parser.error("--pixel-tolerance must be in 0..255")
    if args.diff_gain < 1:
        parser.error("--diff-gain must be positive")
    for name in ("max_changed_fraction",):
        value = getattr(args, name)
        if value is not None and not 0 <= value <= 1:
            parser.error(f"--{name.replace('_', '-')} must be in 0..1")
    if args.max_mean_diff is not None and args.max_mean_diff < 0:
        parser.error("--max-mean-diff must be non-negative")
    if args.pause_settle < 0 or args.seek_settle < 0:
        parser.error("settle times must be non-negative")
    try:
        sample_times(args.start_time, args.end_time, args.step)
    except ValueError as exc:
        parser.error(str(exc))
    if args.contain and (
            sys.platform != "darwin" or shutil.which("sandbox-exec") is None):
        parser.error("--contain requires macOS sandbox-exec")

    results = Path(args.results).expanduser().resolve()
    cfg = make_config(args, results)
    try:
        runner = S.Runner(cfg)
    except S.StressBinaryError as exc:
        parser.error(str(exc))
    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    finding, report = run_probe(runner, cfg, args, seed)
    S.write_report(runner, results / "REPORT.md")
    print(f"visual report: {report}")
    return 1 if finding else 0


if __name__ == "__main__":
    raise SystemExit(main())
