"""Xenia goldens: record the ORIGINAL game's state, check the native port.

Tier A of docs/plans/XENIA_ORACLE.md, step 3. Subcommands:

``capture``  run the loader probe (:mod:`loader`) against one target
             (``xenia:<sock>`` or ``native[:url]``) -> a capture JSON.
``stable``   intersect N captures of ONE side from SEPARATE boots; keep only
             fields that agree in every capture (the side's noise floor).
``record``   stable Xenia captures + a provenance manifest -> a golden bundle.
``compare``  golden vs a capture -> every disagreement, classified against
             the bundle's adjudication table. Exit 1 on any unadjudicated one.
``check``    boot dc3-native headless on a free port, capture, compare. This
             is what the ``XeniaGolden.*`` ctest runs: no Xenia needed.

A golden bundle is a directory::

    manifest.json   provenance: xex sha256, xenia xxh3, full cvar argv,
                    ACTIVE hack ids (from the launch log), probe spec hash,
                    schema hash, boot state, capture count, noise floor
    golden.json     {milo: {object: {field: "<type>:<value>"}}}  (stable only)
    adjudications.json  known disagreements, each with a verdict and reason

The differ REFUSES a manifest whose ``perturbed_subsystems`` covers the probe's
subsystem (the §1/§9 table of the oracle doc, made machine-enforced): a
finding from a patched instrument is not a finding.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import signal
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

from . import loader as L
from .transport import NativeHttpTarget, TransportError, make_target

REPO = Path(__file__).resolve().parents[2]
GOLDEN_SCHEMA = "xenia-golden/v1"
#: The subsystems the loader probe exercises. The differ refuses a golden
#: whose manifest marks any of them perturbed.
LOADER_SUBSYSTEMS = ("dirloader", "object_load", "propsync", "dta_interpreter")

#: Hack-id prefix -> the subsystem it perturbs (xenia docs/fork/dc3/
#: PATCH_MANIFEST.md). Unknown ids map to "unknown", which the differ refuses.
HACK_SUBSYSTEMS = [
    ("nui.", "kinect_nui"),
    ("calib.", "kinect_calibration"),
    ("skel.", "kinect_skeleton_thread"),
    ("seq.controller_mode", "kinect_nui"),
    ("speech.", "speech"),
    ("game.pause_for_skeleton_loss", "gameplay_pause"),
    ("content.", "content_xam"),
    ("saveload.", "content_xam"),
    ("input.attract_press", "ui_flow_input"),
    ("mmio.soft_fault_range", "mmio_decomp_only"),
]


def hack_subsystem(hid: str) -> str:
    for prefix, sub in HACK_SUBSYSTEMS:
        if hid == prefix or hid.startswith(prefix):
            return sub
    return "unknown"


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def probe_spec_hash() -> str:
    """Hash of the code that decides WHAT is read and HOW it is formatted."""
    h = hashlib.sha256()
    for name in ("loader.py", "editor_schema.py"):
        h.update(name.encode() + b"\0" + (Path(__file__).parent / name).read_bytes())
    return h.hexdigest()


# ---------------------------------------------------------------------------
# capture
# ---------------------------------------------------------------------------

def capture(target_spec: str, milos: list[str], *, max_elems: int, log) -> dict:
    target = make_target(target_spec) if target_spec != "native" else NativeHttpTarget()
    if target_spec.startswith("native:"):
        target = NativeHttpTarget(target_spec.partition(":")[2])
    schemas, schema_sha = L.load_default_schemas(REPO)
    probe = L.LoaderProbe(target, schemas, max_elems=max_elems, log=log)
    out = {"schema": GOLDEN_SCHEMA, "probe": "loader", "target": target_spec,
           "probe_spec_sha256": probe_spec_hash(), "editor_schema_sha256": schema_sha,
           "max_elems": max_elems, "captured_at": time.time(), "milos": {}}
    for i, milo in enumerate(milos):
        out["milos"][milo] = probe.capture_milo(milo, f"$xg_d{i}")
    s = probe.stats
    out["stats"] = {"requests": s.requests, "refused_pages": s.refused_pages,
                    "refused_reads": s.refused_reads, "max_script": s.max_script,
                    "max_reply": s.max_reply, "seconds": round(s.seconds, 1)}
    return out


def _flatten(cap: dict) -> dict[tuple, str]:
    flat = {}
    for milo, m in cap["milos"].items():
        for obj, ent in m["objects"].items():
            for k, v in ent.items():
                flat[(milo, obj, k)] = v
    return flat


# ---------------------------------------------------------------------------
# stable
# ---------------------------------------------------------------------------

def stable(caps: list[dict]) -> tuple[dict, dict]:
    """Fields equal in every capture -> golden map; plus a noise report."""
    flats = [_flatten(c) for c in caps]
    keys = set(flats[0])
    for f in flats[1:]:
        keys &= set(f)
    churn = sum(len(set(f) ^ set(flats[0])) for f in flats[1:])
    golden: dict = {}
    unstable = []
    for k in sorted(keys):
        vals = {f[k] for f in flats}
        if len(vals) == 1:
            milo, obj, field = k
            golden.setdefault(milo, {}).setdefault(obj, {})[field] = flats[0][k]
        else:
            unstable.append({"milo": k[0], "object": k[1], "field": k[2],
                             "values": sorted(vals)})
    noise = {"captures": len(caps), "cells": len(keys), "unstable": len(unstable),
             "key_churn": churn, "unstable_cells": unstable[:200]}
    return golden, noise


# ---------------------------------------------------------------------------
# compare
# ---------------------------------------------------------------------------

_NUM = re.compile(r"^-?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$")
#: MS CRT spells non-finite floats differently from glibc.
_NONFINITE = {"1.#QNAN": "nan", "-1.#QNAN": "nan", "1.#IND": "nan", "-1.#IND": "nan",
              "1.#INF": "inf", "-1.#INF": "-inf", "nan": "nan", "-nan": "nan",
              "inf": "inf", "-inf": "-inf"}

#: Relative tolerance under which two DIFFERENT float strings are reported as
#: `fp_eval` (class d) rather than failing. Load() copies bytes, so a loaded
#: value is bit-exact; only DERIVED values (euler angles from a matrix, where
#: PPC fuses multiply-adds and x86 need not) can differ, and only in the last
#: ulps. 1e-6 relative is ~8 ulps of float32.
FP_REL_TOL = 1e-6


#: MS CRT prints a float exponent with at least THREE digits ("1.19209e-007"),
#: glibc with at least two ("1.19209e-07"). Same value, two spellings: this is
#: a representation difference of the two C runtimes (class d), normalised
#: on both sides before comparing and counted, never silently.
_EXP = re.compile(r"([0-9])e([-+])0+([0-9]{2,})")


def norm_crt(v: str) -> str:
    return _EXP.sub(r"\1e\2\3", v) if v else v


def _split(v: str) -> tuple[str, str]:
    t, sep, val = v.partition(":")
    return (t, val) if sep and t.lstrip("-").isdigit() else ("", v)


def classify(gold: str, live: str) -> str | None:
    """None if equal; otherwise a mechanical class for the difference."""
    if gold == live:
        return None
    if live is None:
        return "missing_field"
    if norm_crt(gold) == norm_crt(live):
        return "crt_exponent"
    gold, live = norm_crt(gold), norm_crt(live)
    gt, gv = _split(gold)
    lt, lv = _split(live)
    if L.REFUSED in (gold, live):
        return "refused_one_side"
    if L.ABSENT in (gold, live):
        return "property_presence"
    if gt != lt:
        return "value_type"
    if gt == "1":
        gn, ln = _NONFINITE.get(gv, gv), _NONFINITE.get(lv, lv)
        if gn == ln:
            return None
        if _NUM.match(gn) and _NUM.match(ln):
            a, b = float(gn), float(ln)
            if struct.pack("<f", a) == struct.pack("<f", b):
                # Same float32, two decimal spellings: MS CRT rounds a %.9g
                # tie away from zero, glibc to even (4497.828125 ->
                # "4497.82813" vs "4497.82812"). Measured on PropAnim frame.
                return "crt_round"
            if abs(a - b) <= FP_REL_TOL * max(1.0, abs(a), abs(b)):
                return "fp_eval"
        return "float_value"
    return "value"


def _rule_matches(a: dict, milo: str, obj: str, cls: str, field: str,
                  gv: str, lv: str | None) -> bool:
    """One adjudication entry against one disagreement.

    Entries are deliberately NARROW so a waiver cannot hide a new bug:
    ``milo``/``object``/``class`` are optional exact filters, ``field`` is an
    exact field key or a ``fnmatch`` glob, and ``rule`` is one of
      exact      golden AND live values both equal the recorded pair
      int_delta  both are ints of the same type and live - golden == delta
    """
    import fnmatch
    if a.get("milo") and a["milo"] != milo:
        return False
    if a.get("object") and a["object"] != obj:
        return False
    if a.get("class") and a["class"] != cls:
        return False
    if not fnmatch.fnmatchcase(field, a["field"]):
        return False
    rule = a.get("rule", "exact")
    if rule == "exact":
        return a.get("golden") == gv and a.get("live") == lv
    if rule == "golden_is":
        # The original holds exactly this value, whatever native holds
        # (e.g. DxMesh::OnSync frees CPU faces after upload; native keeps them).
        return a.get("golden") == gv and lv is not None and _split(lv)[0] == _split(gv)[0]
    if rule == "int_delta":
        gt, g = _split(gv)
        lt, l = _split(lv or "")
        try:
            return gt == lt == "0" and int(l) - int(g) == int(a["delta"])
        except ValueError:
            return False
    raise ValueError(f"unknown adjudication rule {rule!r}")


#: Classes a comparison counts but never fails on. Each is a REPRESENTATION
#: difference with a mechanical proof attached (class d in the oracle doc).
TOLERATED = {"crt_exponent", "crt_round", "fp_eval"}


def _multiset_ok(a: dict, milo, obj, ocls, field, golden_fields: dict, live: dict) -> bool:
    """``rule: multiset``: the array under ``a['array']`` holds the same
    VALUES on both sides, in any order. For arrays whose order the engine
    derives from heap addresses (RndDir::SyncDrawables -> SortDraws breaks
    draw_order ties by comparing RndMat POINTERS, rndobj/Utl.cpp:351), so the
    order cannot be reproduced by a different allocator."""
    import fnmatch
    if a.get("rule") != "multiset" or (a.get("class") and a["class"] != ocls):
        return False
    if not fnmatch.fnmatchcase(field, a["field"]):
        return False
    pre = a["array"] + "/"
    idx = re.compile(re.escape(pre) + r"\d+$")
    gvals = sorted(v for k, v in golden_fields.items() if idx.match(k))
    lvals = sorted(v for (m, o, k), v in live.items() if m == milo and o == obj and idx.match(k))
    gsize = golden_fields.get(pre + "#")
    lsize = live.get((milo, obj, pre + "#"))
    return gsize == lsize and gvals == lvals and int(gsize or 0) == len(gvals)


def compare(golden: dict, cap: dict, adjudications: list[dict]) -> dict:
    live = _flatten(cap)
    rows = []
    cells = 0
    for milo, objs in golden.items():
        for obj, fields in objs.items():
            ocls = fields.get("_class", "")
            for field, gv in fields.items():
                cells += 1
                lv = live.get((milo, obj, field))
                cls = classify(gv, lv)
                if cls is None:
                    continue
                a = next((a for a in adjudications if a.get("rule") == "multiset"
                          and _multiset_ok(a, milo, obj, ocls, field, fields, live)), None) \
                    or next((a for a in adjudications if a.get("rule") != "multiset"
                             and _rule_matches(a, milo, obj, ocls, field, gv, lv)), None)
                rows.append({"milo": milo, "object": obj, "class_name": ocls,
                             "field": field, "golden": gv, "live": lv, "class": cls,
                             "verdict": a.get("verdict") if a else None,
                             "adjudication": a.get("id") if a else None})
    # An object the native load produced that the original's did not is a
    # presence difference (State Diff ranks it CRITICAL), not noise: the golden
    # keeps the meta row of every object that was present in every capture.
    extra = sorted({(m, o) for (m, o, _f) in live
                    if m in golden and o not in golden[m]})
    for m, o in extra:
        a = next((a for a in adjudications if a.get("rule") == "extra_object"
                  and a.get("milo") == m and a.get("object") == o), None)
        rows.append({"milo": m, "object": o, "class_name": live.get((m, o, "_class")),
                     "field": "_presence", "golden": "<absent object>", "live": "present",
                     "class": "extra_object", "verdict": a.get("verdict") if a else None,
                     "adjudication": a.get("id") if a else None})
    open_rows = [r for r in rows if not r["verdict"] and r["class"] not in TOLERATED]
    return {"cells": cells, "disagreements": rows, "open": len(open_rows),
            "extra_live_objects": [list(x) for x in extra[:100]],
            "by_class": _count(rows, "class"), "by_verdict": _count(rows, "verdict"),
            "by_adjudication": _count(rows, "adjudication")}


def _count(rows, k):
    out: dict = {}
    for r in rows:
        out[str(r[k])] = out.get(str(r[k]), 0) + 1
    return out


def refuse_if_perturbed(manifest: dict) -> None:
    bad = sorted(set(manifest.get("perturbed_subsystems", [])) & set(LOADER_SUBSYSTEMS))
    unknown = [h for h in manifest.get("active_hacks", []) if hack_subsystem(h) == "unknown"]
    if bad or unknown:
        raise SystemExit(
            f"REFUSED: golden {manifest.get('name')} was recorded with "
            f"{'perturbed subsystems ' + str(bad) if bad else ''}"
            f"{' unknown hacks ' + str(unknown) if unknown else ''} that the loader probe "
            "reads. A finding from a patched instrument is not a finding.")


def print_report(res: dict, out=sys.stdout, limit: int = 60) -> None:
    print(f"compared {res['cells']} golden cells: {len(res['disagreements'])} disagreement(s), "
          f"{res['open']} open (unadjudicated, beyond fp tolerance)", file=out)
    print(f"  by class: {res['by_class']}", file=out)
    print(f"  by verdict: {res['by_verdict']}", file=out)
    print(f"  by adjudication: {res['by_adjudication']}", file=out)
    # OPEN rows first, then adjudicated, then the tolerated representation rows.
    shown = sorted(res["disagreements"], key=lambda r: (
        2 if r["class"] in TOLERATED else (1 if r["verdict"] else 0)))
    for r in shown[:limit]:
        tag = r["verdict"] or ("tolerated" if r["class"] in TOLERATED else "OPEN")
        print(f"  [{tag:>12}] {r['class']:<17} {Path(r['milo']).name}:{r['object']}"
              f" {r['field']}: golden={r['golden']!r} live={r['live']!r}", file=out)
    if len(res["disagreements"]) > limit:
        print(f"  ... {len(res['disagreements']) - limit} more", file=out)


# ---------------------------------------------------------------------------
# provenance
# ---------------------------------------------------------------------------

def xenia_provenance(run_dir: Path) -> dict:
    cmd = (run_dir / "cmd.txt").read_text()
    kv = {}
    for line in cmd.splitlines():
        k, sep, v = line.partition(": ")
        if sep:
            kv[k.strip()] = v.strip()
    log = (run_dir / "run.log").read_text(errors="replace")
    on = sorted(set(re.findall(r"DC3 HACK on: (\S+)", log)))
    off = sorted(set(re.findall(r"DC3 HACK off: (\S+)", log)))
    fired = sorted(set(re.findall(r"DC3 HACK fired: (\S+)", log)))
    tainted = sorted(set(l.strip()[-200:] for l in log.splitlines()
                         if "TAINTED" in l or ("TRIPWIRE" in l and "watching" not in l)))[:20]
    screens = re.findall(r"DC3 Script: screen -> '([^']*)'", log)
    nonxma = re.findall(r"DC3 FAULTS \(\d+ms\): SIGSEGV=\d+ XMA=\d+ NON_XMA=(\d+)", log)
    # Full effective cvar set: the pinned all-defaults config (generated by
    # the fork's own defaults dump) overlaid with the explicit argv.
    cvars = {}
    cfg = run_dir / "config.toml"
    if cfg.exists():
        for line in cfg.read_text(errors="replace").splitlines():
            m = re.match(r"^([A-Za-z0-9_]+)\s*=\s*(.*?)\s*(#.*)?$", line.split("\t#")[0])
            if m:
                cvars[m.group(1)] = m.group(2)
    for tok in re.findall(r"--([A-Za-z0-9_]+)=(\S+)", kv.get("argv", "")):
        cvars[tok[0]] = tok[1]
    return {
        "effective_cvars": cvars,
        "xenia_binary": kv.get("xenia"),
        "xenia_xxh3": kv.get("xenia_xxh3"),
        "xenia_git": kv.get("xenia_git"),
        "xex_sha256": kv.get("xex_sha256"),
        "config_sha256": kv.get("config_sha256"),
        "flow_sha256": kv.get("flow_sha256"),
        "argv": kv.get("argv"),
        "load_start": kv.get("load_start"),
        "active_hacks": on,
        "disabled_hacks": off,
        "fired_hacks": fired,
        "tripwire_lines": tainted,
        "screens": screens,
        "max_non_xma_faults": max((int(x) for x in nonxma), default=None),
    }


# ---------------------------------------------------------------------------
# native boot for `check`
# ---------------------------------------------------------------------------

def _free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class NativeBoot:
    """dc3-native headless with the HTTP server on a private port."""

    def __init__(self, binary: Path, log_path: Path, timeout_s: int = 600,
                 extra_env: dict | None = None):
        self.binary = binary
        self.port = _free_port()
        env = dict(os.environ)
        env.update({"MILO_HEADLESS": "1", "DC3_FAST_TIME": "1", "DC3_HTTP": "1",
                    "DC3_HTTP_PORT": str(self.port), "DC3_FAST_BOOT": "0", "DC3_TEL": "0",
                    "DC3_SHOW_SPLASH": "0", "MILO_MAX_FRAMES": "0"})
        env.pop("MILO_INPUT_SCRIPT", None)
        env.update(extra_env or {})
        self.log = open(log_path, "wb")
        self.proc = subprocess.Popen(
            ["timeout", "-k", "5", str(timeout_s), str(binary)],
            cwd=str(binary.parent), env=env, stdout=self.log, stderr=subprocess.STDOUT,
            start_new_session=True)
        self.url = f"http://127.0.0.1:{self.port}"
        self.boot_screen = "title_screen"

    def wait_ready(self, deadline_s: float = 180.0) -> NativeHttpTarget:
        t = NativeHttpTarget(self.url, timeout=30.0)
        end = time.time() + deadline_s
        while time.time() < end:
            if self.proc.poll() is not None:
                raise RuntimeError(f"dc3-native exited rc={self.proc.returncode} before ready")
            try:
                r = t.eval_dta("{+ 1 1}", timeout=5.0)
                if r.ok and str(r.value) == "2":
                    # Let boot settle past the first screen.
                    scr = t.eval_dta("{if_else {ui current_screen} {{ui current_screen} name} none}")
                    if scr.ok and scr.text == self.boot_screen:
                        time.sleep(2.0)  # let the screen's enter settle
                        return t
            except TransportError:
                pass
            time.sleep(1.0)
        raise RuntimeError(f"dc3-native not ready on {self.url} in {deadline_s}s")

    def stop(self):
        if self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
                self.proc.wait(timeout=15)
            except Exception:  # noqa: BLE001
                os.killpg(self.proc.pid, signal.SIGKILL)
        self.log.close()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _log(msg):
    print(msg, file=sys.stderr, flush=True)


def cmd_capture(a):
    if a.target == "native-boot":
        # A FRESH dc3-native boot on a private port, so each capture is its
        # own boot (the noise floor needs separate boots, not repeat reads).
        work = Path(a.output).with_suffix(".native.log")
        boot = NativeBoot(Path(a.native_binary).resolve(), work)
        try:
            boot.wait_ready()
            cap = capture("native:" + boot.url, a.milo, max_elems=a.max_elems, log=_log)
        finally:
            boot.stop()
    else:
        cap = capture(a.target, a.milo, max_elems=a.max_elems, log=_log)
    Path(a.output).write_text(json.dumps(cap, indent=1, sort_keys=True))
    _log(f"wrote {a.output}: {cap['stats']}")


def cmd_stable(a):
    caps = [json.loads(Path(p).read_text()) for p in a.captures]
    golden, noise = stable(caps)
    Path(a.output).write_text(json.dumps({"golden": golden, "noise": noise},
                                         indent=1, sort_keys=True))
    _log(f"{noise['cells']} cells, {noise['unstable']} unstable, churn {noise['key_churn']}")


def cmd_record(a):
    caps = [json.loads(Path(p).read_text()) for p in a.captures]
    if a.milo:
        for c in caps:
            missing = [m for m in a.milo if m not in c["milos"]]
            if missing:
                raise SystemExit(f"capture lacks {missing}")
            c["milos"] = {m: c["milos"][m] for m in a.milo}
    for c in caps:
        if c["probe_spec_sha256"] != caps[0]["probe_spec_sha256"]:
            raise SystemExit("captures were taken with different probe specs")
    golden, noise = stable(caps)
    runs = [xenia_provenance(Path(r)) for r in a.run_dir]
    for key in ("xenia_xxh3", "xex_sha256", "config_sha256", "flow_sha256"):
        vals = {r[key] for r in runs}
        if len(vals) != 1:
            raise SystemExit(f"runs disagree on {key}: {vals}")
    hacks = sorted(set().union(*(r["active_hacks"] for r in runs)))
    perturbed = sorted({hack_subsystem(h) for h in hacks})
    bundle = Path(a.bundle)
    bundle.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": GOLDEN_SCHEMA,
        "name": bundle.name,
        "probe": "loader",
        "probe_subsystems": list(LOADER_SUBSYSTEMS),
        "milos": list(caps[0]["milos"]),
        "boot_state": "title_screen (flow golden/title_only.flow.txt; nothing pressed after it)",
        "probe_spec_sha256": caps[0]["probe_spec_sha256"],
        "editor_schema_sha256": caps[0]["editor_schema_sha256"],
        "max_elems": caps[0]["max_elems"],
        "xex_sha256": runs[0]["xex_sha256"],
        "xenia_xxh3": runs[0]["xenia_xxh3"],
        "xenia_git": runs[0]["xenia_git"],
        "config_sha256": runs[0]["config_sha256"],
        "flow_sha256": runs[0]["flow_sha256"],
        "argv": [r["argv"] for r in runs],
        "effective_cvars": runs[0]["effective_cvars"],
        "active_hacks": hacks,
        "fired_hacks": sorted(set().union(*(r["fired_hacks"] for r in runs))),
        "perturbed_subsystems": perturbed,
        "runs": [{k: r[k] for k in ("load_start", "screens", "tripwire_lines",
                                    "max_non_xma_faults")} for r in runs],
        "capture_stats": [c["stats"] for c in caps],
        "noise": {k: v for k, v in noise.items() if k != "unstable_cells"},
        "unstable_cells": noise["unstable_cells"],
        "golden_sha256": None,
    }
    gtext = json.dumps(golden, indent=1, sort_keys=True)
    manifest["golden_sha256"] = _sha256(gtext.encode())
    (bundle / "golden.json").write_text(gtext + "\n")
    (bundle / "manifest.json").write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")
    adj = bundle / "adjudications.json"
    if not adj.exists():
        adj.write_text("[]\n")
    refuse_if_perturbed(manifest)
    _log(f"bundle {bundle}: {noise['cells']} cells, {noise['unstable']} unstable dropped, "
         f"hacks {len(hacks)} -> subsystems {perturbed}")


def _load_bundle(path: Path):
    manifest = json.loads((path / "manifest.json").read_text())
    gtext = (path / "golden.json").read_text()
    if _sha256(gtext.rstrip("\n").encode()) != manifest["golden_sha256"]:
        raise SystemExit(f"{path}/golden.json does not match manifest golden_sha256")
    refuse_if_perturbed(manifest)
    adj = json.loads((path / "adjudications.json").read_text()) \
        if (path / "adjudications.json").exists() else []
    return manifest, json.loads(gtext), adj


def cmd_compare(a):
    manifest, golden, adj = _load_bundle(Path(a.bundle))
    cap = json.loads(Path(a.capture).read_text())
    if cap["probe_spec_sha256"] != manifest["probe_spec_sha256"] and not a.allow_spec_drift:
        raise SystemExit("capture probe spec differs from the golden's; re-record or "
                         "pass --allow-spec-drift")
    res = compare(golden, cap, adj)
    print_report(res, limit=a.limit)
    if a.json:
        Path(a.json).write_text(json.dumps(res, indent=1))
    sys.exit(1 if res["open"] else 0)


def cmd_check(a):
    bundle = Path(a.bundle)
    # Bundles are property dumps of shipped .milo content, so they live in the
    # gitignored archive/ (archive/state_diff/goldens/xenia/<name>), never in
    # git. A tree without them skips (77) and says why; it does not pass.
    if not (bundle / "golden.json").exists() or not (bundle / "manifest.json").exists():
        print(f"SKIP: no Xenia golden bundle at {bundle} (game-derived data, kept "
              "in the gitignored archive/; see docs/tools/STATE_DIFF.md)", file=sys.stderr)
        sys.exit(77)
    manifest, golden, adj = _load_bundle(bundle)
    binary = Path(a.native_binary).resolve()
    if not binary.exists():
        print(f"SKIP: {binary} not built", file=sys.stderr)
        sys.exit(77)
    work = Path(a.work_dir or f"/tmp/xenia-golden-check-{os.getpid()}")
    work.mkdir(parents=True, exist_ok=True)
    extra_env = dict(kv.split("=", 1) for kv in a.env)
    boot = NativeBoot(binary, work / "native.log", extra_env=extra_env)
    try:
        target = boot.wait_ready()
        schemas, _sha = L.load_default_schemas(REPO)
        probe = L.LoaderProbe(target, schemas, max_elems=manifest["max_elems"], log=_log)
        cap = {"schema": GOLDEN_SCHEMA, "probe": "loader", "target": boot.url,
               "probe_spec_sha256": probe_spec_hash(), "milos": {}}
        for i, milo in enumerate(manifest["milos"]):
            cap["milos"][milo] = probe.capture_milo(milo, f"$xg_d{i}")
    finally:
        boot.stop()
    (work / "native_capture.json").write_text(json.dumps(cap, indent=1, sort_keys=True))
    if cap["probe_spec_sha256"] != manifest["probe_spec_sha256"]:
        print("FAIL: the probe spec changed since the golden was recorded "
              f"({manifest['probe_spec_sha256'][:12]} -> {cap['probe_spec_sha256'][:12]}); "
              "re-record the golden under Xenia", file=sys.stderr)
        sys.exit(1)
    res = compare(golden, cap, adj)
    print(f"golden {manifest['name']}: xenia {manifest['xenia_xxh3']} xex "
          f"{manifest['xex_sha256'][:12]} hacks-active {len(manifest['active_hacks'])} "
          f"perturbed {manifest['perturbed_subsystems']}")
    print_report(res, limit=a.limit)
    (work / "compare.json").write_text(json.dumps(res, indent=1))
    sys.exit(1 if res["open"] else 0)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="state_diff.golden", description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--target", required=True,
                   help="xenia:<sock> | native | native:<url> | native-boot")
    c.add_argument("--native-binary", default=str(REPO / "native/build/dc3-native"))
    c.add_argument("--milo", action="append", required=True)
    c.add_argument("--max-elems", type=int, default=64)
    c.add_argument("-o", "--output", required=True)
    c.set_defaults(fn=cmd_capture)
    s = sub.add_parser("stable")
    s.add_argument("captures", nargs="+")
    s.add_argument("-o", "--output", required=True)
    s.set_defaults(fn=cmd_stable)
    r = sub.add_parser("record")
    r.add_argument("--bundle", required=True)
    r.add_argument("--capture", dest="captures", action="append", required=True)
    r.add_argument("--run-dir", action="append", required=True)
    r.add_argument("--milo", action="append", help="keep only these milo specs")
    r.set_defaults(fn=cmd_record)
    m = sub.add_parser("compare")
    m.add_argument("bundle")
    m.add_argument("capture")
    m.add_argument("--json")
    m.add_argument("--limit", type=int, default=80)
    m.add_argument("--allow-spec-drift", action="store_true")
    m.set_defaults(fn=cmd_compare)
    k = sub.add_parser("check")
    k.add_argument("bundle")
    k.add_argument("--native-binary", default=str(REPO / "native/build/dc3-native"))
    k.add_argument("--work-dir")
    k.add_argument("--env", action="append", default=[], help="KEY=VALUE for dc3-native")
    k.add_argument("--limit", type=int, default=80)
    k.set_defaults(fn=cmd_check)
    a = ap.parse_args(argv)
    a.fn(a)


if __name__ == "__main__":
    main()
