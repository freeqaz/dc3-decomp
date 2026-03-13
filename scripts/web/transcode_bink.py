#!/usr/bin/env python3
"""Transcode Bink assets into browser-native WebM sidecars for the web port.

The default output layout keeps the relative path stable:

    orig-assets/extracted/videos/intro.bik
    -> orig-assets/extracted/videos/intro.webm

That lets the future web runtime resolve `.bik` requests to `.webm` without
changing the logical asset names stored in DTA/config data.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import fnmatch
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


BINK_SUFFIXES = {".bik", ".bk2", ".bik2"}
DEFAULT_ASSETS_ROOT = Path("orig-assets/extracted")
DEFAULT_MANIFEST_NAME = "web-video-manifest.json"


@dataclass(frozen=True)
class ProbeInfo:
    rel_source: str
    abs_source: Path
    rel_output: str
    abs_output: Path
    width: int
    height: int
    fps: float
    duration_sec: float
    pix_fmt: str
    has_alpha: bool
    has_audio: bool
    audio_channels: int
    audio_sample_rate: int
    source_size: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Transcode Bink assets to WebM sidecars for the browser build."
    )
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=DEFAULT_ASSETS_ROOT,
        help="Root directory containing extracted game assets.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=None,
        help="Where generated .webm files should be written. Defaults to --assets-root.",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=None,
        help="Manifest path. Defaults to <output-root>/web-video-manifest.json.",
    )
    parser.add_argument(
        "--match",
        action="append",
        default=[],
        help="Only process relative asset paths matching this glob. Repeatable.",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=0,
        help="Only process the first N matching Bink files after sorting.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, (os.cpu_count() or 1) // 2),
        help="Number of parallel ffmpeg workers.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Re-encode outputs even if the .webm sidecar is newer than the source.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned work without invoking ffmpeg.",
    )
    parser.add_argument(
        "--crf",
        type=int,
        default=30,
        help="libvpx-vp9 constant quality factor. Lower is higher quality.",
    )
    parser.add_argument(
        "--cpu-used",
        type=int,
        default=2,
        help="libvpx-vp9 speed/quality tradeoff (0=slowest, 8=fastest).",
    )
    parser.add_argument(
        "--deadline",
        choices=("good", "best", "realtime"),
        default="good",
        help="libvpx-vp9 encoder deadline preset.",
    )
    parser.add_argument(
        "--audio-bitrate",
        default="128k",
        help="Opus bitrate for videos that carry audio.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print every ffmpeg command before executing it.",
    )
    return parser.parse_args()


def require_tool(name: str) -> None:
    if shutil.which(name):
        return
    raise SystemExit(f"missing required tool: {name}")


def parse_rate(rate: str | None) -> float:
    if not rate or rate in {"0/0", "N/A"}:
        return 0.0
    if "/" in rate:
        num_str, den_str = rate.split("/", 1)
        num = float(num_str)
        den = float(den_str)
        return num / den if den else 0.0
    return float(rate)


def probe_media(path: Path, assets_root: Path, output_root: Path) -> ProbeInfo:
    result = subprocess.run(
        [
            "ffprobe",
            "-v",
            "error",
            "-show_streams",
            "-show_format",
            "-of",
            "json",
            str(path),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    data = json.loads(result.stdout)
    streams = data.get("streams", [])

    video_stream = next((s for s in streams if s.get("codec_type") == "video"), None)
    if not video_stream:
        raise RuntimeError(f"no video stream found in {path}")

    audio_stream = next((s for s in streams if s.get("codec_type") == "audio"), None)
    rel_source = path.relative_to(assets_root).as_posix()
    rel_output = str(Path(rel_source).with_suffix(".webm")).replace("\\", "/")

    pix_fmt = video_stream.get("pix_fmt", "")
    duration_str = (
        data.get("format", {}).get("duration")
        or video_stream.get("duration")
        or "0"
    )

    return ProbeInfo(
        rel_source=rel_source,
        abs_source=path,
        rel_output=rel_output,
        abs_output=output_root / rel_output,
        width=int(video_stream.get("width") or 0),
        height=int(video_stream.get("height") or 0),
        fps=parse_rate(video_stream.get("r_frame_rate")),
        duration_sec=float(duration_str),
        pix_fmt=pix_fmt,
        has_alpha="a" in pix_fmt,
        has_audio=audio_stream is not None,
        audio_channels=int(audio_stream.get("channels") or 0) if audio_stream else 0,
        audio_sample_rate=int(audio_stream.get("sample_rate") or 0) if audio_stream else 0,
        source_size=path.stat().st_size,
    )


def should_include(rel_path: str, patterns: list[str]) -> bool:
    if not patterns:
        return True
    return any(fnmatch.fnmatch(rel_path, pattern) for pattern in patterns)


def find_inputs(assets_root: Path, patterns: list[str], limit: int) -> list[Path]:
    matches: list[Path] = []
    for path in sorted(assets_root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() not in BINK_SUFFIXES:
            continue
        rel = path.relative_to(assets_root).as_posix()
        if should_include(rel, patterns):
            matches.append(path)
    if limit > 0:
        return matches[:limit]
    return matches


def build_ffmpeg_command(info: ProbeInfo, args: argparse.Namespace) -> list[str]:
    cmd = [
        "ffmpeg",
        "-y",
        "-v",
        "error",
        "-i",
        str(info.abs_source),
        "-map",
        "0:v:0",
        "-c:v",
        "libvpx-vp9",
        "-crf",
        str(args.crf),
        "-b:v",
        "0",
        "-deadline",
        args.deadline,
        "-cpu-used",
        str(args.cpu_used),
        "-row-mt",
        "1",
        "-pix_fmt",
        "yuva420p" if info.has_alpha else "yuv420p",
    ]
    if info.has_audio:
        cmd += [
            "-map",
            "0:a:0",
            "-c:a",
            "libopus",
            "-b:a",
            args.audio_bitrate,
        ]
    else:
        cmd.append("-an")
    cmd.append(str(info.abs_output))
    return cmd


def output_is_fresh(info: ProbeInfo) -> bool:
    if not info.abs_output.is_file():
        return False
    return info.abs_output.stat().st_mtime >= info.abs_source.stat().st_mtime


def encode_one(info: ProbeInfo, args: argparse.Namespace) -> dict[str, Any]:
    info.abs_output.parent.mkdir(parents=True, exist_ok=True)
    command = build_ffmpeg_command(info, args)

    if args.verbose or args.dry_run:
        print(" ".join(command))

    if args.dry_run:
        return {"status": "dry-run", "output_size": 0}

    proc = subprocess.run(command, capture_output=True, text=True)
    if proc.returncode != 0:
        return {
            "status": "failed",
            "stderr": proc.stderr.strip(),
            "stdout": proc.stdout.strip(),
        }
    return {
        "status": "encoded",
        "output_size": info.abs_output.stat().st_size,
    }


def manifest_entry(info: ProbeInfo, transcode_status: str, output_size: int) -> dict[str, Any]:
    return {
        "source": info.rel_source,
        "output": info.rel_output,
        "status": transcode_status,
        "video": {
            "width": info.width,
            "height": info.height,
            "fps": info.fps,
            "duration_sec": info.duration_sec,
            "pix_fmt": info.pix_fmt,
            "has_alpha": info.has_alpha,
            "codec": "vp9",
        },
        "audio": {
            "present": info.has_audio,
            "codec": "opus" if info.has_audio else None,
            "channels": info.audio_channels,
            "sample_rate": info.audio_sample_rate,
        },
        "source_size": info.source_size,
        "output_size": output_size,
    }


def main() -> int:
    args = parse_args()
    require_tool("ffmpeg")
    require_tool("ffprobe")

    assets_root = args.assets_root.resolve()
    if not assets_root.is_dir():
        raise SystemExit(f"assets root does not exist: {assets_root}")

    output_root = args.output_root.resolve() if args.output_root else assets_root
    manifest_path = (
        args.manifest.resolve()
        if args.manifest
        else output_root / DEFAULT_MANIFEST_NAME
    )

    inputs = find_inputs(assets_root, args.match, args.limit)
    if not inputs:
        print("no matching Bink assets found")
        return 0

    print(f"assets root:   {assets_root}")
    print(f"output root:   {output_root}")
    print(f"manifest path: {manifest_path}")
    print(f"matched files: {len(inputs)}")

    probes = [probe_media(path, assets_root, output_root) for path in inputs]

    to_encode: list[ProbeInfo] = []
    entries: list[dict[str, Any]] = []
    skipped = 0

    for info in probes:
        if not args.force and output_is_fresh(info):
            skipped += 1
            output_size = info.abs_output.stat().st_size
            entries.append(manifest_entry(info, "up-to-date", output_size))
            continue
        to_encode.append(info)

    print(f"up-to-date:    {skipped}")
    print(f"to encode:     {len(to_encode)}")

    failures: list[tuple[ProbeInfo, dict[str, Any]]] = []

    if args.dry_run:
        for info in to_encode:
            entries.append(manifest_entry(info, "dry-run", 0))
            encode_one(info, args)
    elif to_encode:
        max_workers = max(1, args.jobs)
        with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as pool:
            future_map = {pool.submit(encode_one, info, args): info for info in to_encode}
            for future in concurrent.futures.as_completed(future_map):
                info = future_map[future]
                result = future.result()
                status = result["status"]
                if status == "failed":
                    failures.append((info, result))
                    entries.append(manifest_entry(info, "failed", 0))
                    print(f"failed: {info.rel_source}")
                    continue
                output_size = int(result.get("output_size") or 0)
                entries.append(manifest_entry(info, status, output_size))
                print(f"{status}: {info.rel_source} -> {info.rel_output}")

    entries.sort(key=lambda item: item["source"])
    manifest = {
        "version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "assets_root": assets_root.as_posix(),
        "output_root": output_root.as_posix(),
        "format": "webm",
        "encoder": {
            "video_codec": "libvpx-vp9",
            "audio_codec": "libopus",
            "crf": args.crf,
            "cpu_used": args.cpu_used,
            "deadline": args.deadline,
            "audio_bitrate": args.audio_bitrate,
        },
        "entries": entries,
    }

    if args.dry_run and args.manifest is None:
        print("dry-run: manifest not written (pass --manifest to override)")
    else:
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"wrote manifest: {manifest_path}")

    if failures:
        print("")
        print(f"{len(failures)} transcodes failed:")
        for info, result in failures[:10]:
            print(f"- {info.rel_source}")
            stderr = result.get("stderr")
            if stderr:
                print(f"  {stderr.splitlines()[-1]}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
