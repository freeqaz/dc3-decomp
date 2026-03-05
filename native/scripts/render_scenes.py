#!/usr/bin/env python3
"""DC3 Native Port — YAML Scene Renderer

Reads a YAML scene definition file and renders screenshots/videos
using milo-viewer in parallel.

Usage:
    python render_scenes.py <yaml>                    # Render all shots
    python render_scenes.py <yaml> --shot aubrey_front # Render one shot
    python render_scenes.py <yaml> --scene aubrey_*   # Render all shots for matching scenes
    python render_scenes.py <yaml> --dry-run           # Print commands without executing
    python render_scenes.py <yaml> --list              # List all defined shots
    python render_scenes.py <yaml> --jobs 8            # Parallel workers (default from YAML or 4)
"""

import argparse
import fnmatch
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

try:
    import yaml
except ImportError:
    print("Error: PyYAML required. Install with: pip install pyyaml", file=sys.stderr)
    sys.exit(1)


def resolve_path(base: str, rel: str) -> str:
    """Resolve a relative milo path against the milo_lib base."""
    return os.path.join(os.path.expanduser(base), rel)


def build_command(shot: dict, scene: dict, settings: dict, viewer: str) -> list[str]:
    """Build a milo-viewer CLI command from shot + scene + settings."""
    milo_lib = os.path.expanduser(settings.get("milo_lib", "."))
    width = shot.get("width", settings.get("width", 1280))
    height = shot.get("height", settings.get("height", 720))
    output_dir = os.path.expanduser(shot.get("output_dir", settings.get("output_dir", "archive/renders")))
    os.makedirs(output_dir, exist_ok=True)

    # Primary file is always the character (has meshes); venue is --subdir
    character = scene.get("character")
    venue = scene.get("venue")
    primary = scene.get("primary")  # explicit override

    if primary:
        milo_file = resolve_path(milo_lib, primary)
    elif character:
        milo_file = resolve_path(milo_lib, character)
    elif venue:
        milo_file = resolve_path(milo_lib, venue)
    else:
        raise ValueError(f"Scene has no character, venue, or primary file")

    cmd = [viewer, milo_file, "--width", str(width), "--height", str(height)]

    # Venue as subdir (when character is primary)
    if character and venue:
        cmd += ["--subdir", resolve_path(milo_lib, venue)]
        # Venue offset
        venue_offset = scene.get("venue_offset")
        if venue_offset:
            cmd += ["--subdir-offset", str(venue_offset.get("x", 0)),
                     str(venue_offset.get("y", 0)), str(venue_offset.get("z", 0))]
        venue_rotate = scene.get("venue_rotate")
        if venue_rotate is not None:
            cmd += ["--subdir-rotate", str(venue_rotate)]

    # Additional subdirs
    for sd in scene.get("subdirs", []):
        if isinstance(sd, str):
            cmd += ["--subdir", resolve_path(milo_lib, sd)]
        elif isinstance(sd, dict):
            cmd += ["--subdir", resolve_path(milo_lib, sd["path"])]
            if "offset" in sd:
                o = sd["offset"]
                cmd += ["--subdir-offset", str(o.get("x", 0)), str(o.get("y", 0)), str(o.get("z", 0))]
            if "rotate" in sd:
                cmd += ["--subdir-rotate", str(sd["rotate"])]

    # Char setup (FileMerger-based loading)
    char_setup = scene.get("char_setup")
    if char_setup:
        cmd += ["--char-setup", resolve_path(milo_lib, char_setup)]

    # Clips
    clips = scene.get("clips")
    if clips:
        cmd += ["--clips", resolve_path(milo_lib, clips)]

    # Visemes (facial animation)
    visemes = scene.get("visemes")
    if visemes:
        cmd += ["--visemes", resolve_path(milo_lib, visemes)]

    clip_name = shot.get("clip", scene.get("clip"))
    if clip_name:
        cmd += ["--clip", str(clip_name)]

    bpm = shot.get("bpm", scene.get("bpm"))
    if bpm:
        cmd += ["--bpm", str(bpm)]

    # Camera
    camera = shot.get("camera", {})
    cam_mode = camera.get("mode")
    if cam_mode:
        cmd += ["--camera", cam_mode]
    if "azimuth" in camera:
        cmd += ["--azimuth", str(camera["azimuth"])]
    if "elevation" in camera:
        cmd += ["--elevation", str(camera["elevation"])]
    if "distance" in camera:
        cmd += ["--distance", str(camera["distance"])]
    if "eye" in camera:
        e = camera["eye"]
        cmd += ["--eye", str(e[0]), str(e[1]), str(e[2])]
    if "lookat" in camera:
        la = camera["lookat"]
        cmd += ["--lookat", str(la[0]), str(la[1]), str(la[2])]

    # Animation
    if "frame" in shot:
        cmd += ["--frame", str(shot["frame"])]
    if "speed" in shot:
        cmd += ["--speed", str(shot["speed"])]
    if shot.get("paused"):
        cmd += ["--paused"]

    # Lights
    for light in shot.get("lights", scene.get("lights", [])):
        ltype = light.get("type", "dir")
        pos = light.get("pos", light.get("dir", [0, -1, 0]))
        col = light.get("color", [1, 1, 1])
        intensity = light.get("intensity", 1.0)
        cmd += ["--light", ltype, str(pos[0]), str(pos[1]), str(pos[2]),
                str(col[0]), str(col[1]), str(col[2]), str(intensity)]

    ambient = shot.get("ambient", scene.get("ambient"))
    if ambient:
        cmd += ["--ambient", str(ambient[0]), str(ambient[1]), str(ambient[2])]

    # Hide patterns
    for pat in shot.get("hide", scene.get("hide", [])):
        cmd += ["--hide", pat]

    # Output: screenshot or video
    shot_type = shot.get("type", "screenshot")
    shot_name = shot["_name"]

    if shot_type == "video":
        ext = shot.get("format", "mp4")
        out_path = os.path.join(output_dir, f"{shot_name}.{ext}")
        cmd += ["--video", out_path]
        duration = shot.get("duration", 10)
        cmd += ["--duration", str(duration)]
        fps = shot.get("fps", 30)
        cmd += ["--fps", str(fps)]
    else:
        out_path = os.path.join(output_dir, f"{shot_name}.png")
        cmd += ["--screenshot", out_path]

    return cmd, out_path


def run_shot(cmd: list[str], out_path: str, shot_name: str, timeout_sec: int = 120) -> dict:
    """Execute a single render command and return result info."""
    t0 = time.monotonic()
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout_sec
        )
        elapsed = time.monotonic() - t0

        # Check for GPU init failure (exit code 2)
        if result.returncode != 0:
            stderr_tail = result.stderr[-500:] if result.stderr else ""
            if result.returncode == 2 or "GPU initialization failed" in (result.stderr or ""):
                return {"name": shot_name, "status": "GPU_FAIL", "time": elapsed,
                        "stderr": stderr_tail}
            return {"name": shot_name, "status": "FAIL", "time": elapsed,
                    "stderr": stderr_tail}

        if os.path.isfile(out_path) and os.path.getsize(out_path) > 0:
            size = os.path.getsize(out_path)
            # Detect suspiciously small output (black/grey frames from GPU failure)
            is_video = out_path.endswith((".mp4", ".webm", ".mkv"))
            min_size = 100_000 if is_video else 10_000  # real video >100KB, real screenshot >10KB
            if size < min_size:
                return {"name": shot_name, "status": "DARK", "size": size, "time": elapsed,
                        "stderr": f"Suspiciously small ({size:,} bytes) — GPU may have failed"}
            return {"name": shot_name, "status": "OK", "size": size, "time": elapsed}
        else:
            return {"name": shot_name, "status": "FAIL", "time": elapsed,
                    "stderr": result.stderr[-500:] if result.stderr else ""}
    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - t0
        return {"name": shot_name, "status": "TIMEOUT", "time": elapsed}
    except Exception as e:
        elapsed = time.monotonic() - t0
        return {"name": shot_name, "status": "ERROR", "time": elapsed, "error": str(e)}


def main():
    parser = argparse.ArgumentParser(description="DC3 YAML Scene Renderer")
    parser.add_argument("yaml_file", help="Path to YAML scene definition")
    parser.add_argument("--shot", help="Render only shots matching this pattern (glob)")
    parser.add_argument("--scene", help="Render only shots using scenes matching this pattern (glob)")
    parser.add_argument("--dry-run", action="store_true", help="Print commands without executing")
    parser.add_argument("--list", action="store_true", help="List all shots and exit")
    parser.add_argument("--jobs", type=int, default=0, help="Parallel workers (0 = use YAML setting)")
    parser.add_argument("--viewer", help="Path to milo-viewer binary")
    parser.add_argument("--timeout", type=int, default=120, help="Per-shot timeout in seconds")
    args = parser.parse_args()

    # Load YAML
    with open(args.yaml_file) as f:
        cfg = yaml.safe_load(f)

    settings = cfg.get("settings", {})
    scenes = cfg.get("scenes", {})
    shots = cfg.get("shots", {})

    if not shots:
        print("No shots defined in YAML", file=sys.stderr)
        sys.exit(1)

    # Resolve viewer path
    viewer = args.viewer
    if not viewer:
        viewer = settings.get("viewer")
    if not viewer:
        # Auto-detect from project structure
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_dir = os.path.dirname(os.path.dirname(script_dir))
        viewer = os.path.join(project_dir, "native", "build", "milo-viewer")
    if not os.path.isfile(viewer):
        print(f"Error: milo-viewer not found at {viewer}", file=sys.stderr)
        print("  Build: cd native/build && cmake --build . --target milo-viewer", file=sys.stderr)
        sys.exit(1)

    jobs = args.jobs if args.jobs > 0 else settings.get("jobs", 4)

    # ASAN options for milo-viewer
    os.environ.setdefault("ASAN_OPTIONS",
        "alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0")

    # Build shot list
    shot_cmds = []
    for shot_name, shot_def in shots.items():
        if shot_def is None:
            shot_def = {}
        scene_name = shot_def.get("scene")
        if not scene_name:
            print(f"Warning: shot '{shot_name}' has no scene, skipping", file=sys.stderr)
            continue

        # Filter by --scene
        if args.scene and not fnmatch.fnmatch(scene_name, args.scene):
            continue
        # Filter by --shot
        if args.shot and not fnmatch.fnmatch(shot_name, args.shot):
            continue

        scene_def = scenes.get(scene_name)
        if not scene_def:
            print(f"Warning: shot '{shot_name}' references unknown scene '{scene_name}', skipping",
                  file=sys.stderr)
            continue

        shot_def["_name"] = shot_name
        try:
            cmd, out_path = build_command(shot_def, scene_def, settings, viewer)
            shot_cmds.append((shot_name, cmd, out_path))
        except Exception as e:
            print(f"Warning: shot '{shot_name}': {e}", file=sys.stderr)

    if not shot_cmds:
        print("No shots to render (check --shot/--scene filters)")
        sys.exit(0)

    # --list mode
    if args.list:
        print(f"{'Shot':<30} {'Scene':<25} {'Type':<12} {'Output'}")
        print("-" * 90)
        for shot_name, cmd, out_path in shot_cmds:
            scene_name = shots[shot_name].get("scene", "?")
            shot_type = shots[shot_name].get("type", "screenshot")
            print(f"{shot_name:<30} {scene_name:<25} {shot_type:<12} {out_path}")
        print(f"\nTotal: {len(shot_cmds)} shots")
        return

    # --dry-run mode
    if args.dry_run:
        for shot_name, cmd, out_path in shot_cmds:
            # Quote args with spaces
            quoted = []
            for c in cmd:
                if " " in c:
                    quoted.append(f'"{c}"')
                else:
                    quoted.append(c)
            print(f"# {shot_name}")
            print(" ".join(quoted))
            print()
        print(f"# Total: {len(shot_cmds)} shots")
        return

    # Execute
    print(f"=== DC3 Scene Renderer ===")
    print(f"YAML:   {args.yaml_file}")
    print(f"Shots:  {len(shot_cmds)}")
    print(f"Jobs:   {jobs}")
    print()

    results = []
    if jobs == 1 or len(shot_cmds) == 1:
        for shot_name, cmd, out_path in shot_cmds:
            r = run_shot(cmd, out_path, shot_name, args.timeout)
            _print_result(r)
            results.append(r)
    else:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {}
            for shot_name, cmd, out_path in shot_cmds:
                fut = pool.submit(run_shot, cmd, out_path, shot_name, args.timeout)
                futures[fut] = shot_name
            for fut in as_completed(futures):
                r = fut.result()
                _print_result(r)
                results.append(r)

    # Summary
    ok = sum(1 for r in results if r["status"] == "OK")
    dark = sum(1 for r in results if r["status"] == "DARK")
    fail = sum(1 for r in results if r["status"] in ("FAIL", "ERROR", "TIMEOUT", "GPU_FAIL"))
    total = len(results)
    print()
    print(f"=== Results: {ok} OK, {dark} dark, {fail} failed / {total} total ===")

    if fail > 0:
        sys.exit(1)


def _print_result(r: dict):
    name = r["name"]
    status = r["status"]
    elapsed = r.get("time", 0)
    size = r.get("size", 0)
    extra = ""
    if size:
        extra = f"  ({size:,} bytes)"
    if r.get("stderr"):
        extra += f"  {r['stderr'][:80]}"
    if r.get("error"):
        extra += f"  {r['error'][:80]}"
    print(f"  {name:<30} {status:<8} {elapsed:5.1f}s{extra}")


if __name__ == "__main__":
    main()
