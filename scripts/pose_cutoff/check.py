#!/usr/bin/env python3
"""check.py -- the cut-off replay gate (PLAN H-95).

  python3 scripts/pose_cutoff/check.py MATRIX_OUT [--mode kinematic] [--base off]
      [--family img] [--cut knees] [--min-frac 0.75] [--margin 0.02]
  python3 scripts/pose_cutoff/check.py --selftest

MATRIX_OUT is a matrix.py run directory holding, for the family's variants
(img: img_<cut>/imgwrong_<cut>/imgstill_<cut>, image-level crops of the
footage re-tracked by MediaPipe; skel: right_/wrong_/still_<cut>, the
skeleton with the cut joints nulled), a --base run and a --mode run.

Passes when, under --mode:
  1. the right dance with the cut scores at least --min-frac of the right
     dance uncut under --base (what the estimation is for), and
  2. each control -- another song's dance (wrong), a player standing still
     (still) -- scores no more with the cut under --mode than uncut under
     --base, plus --margin x the uncut right dance's score (what it must
     not do: credit a dancer who is not dancing this song).

Exit 0 pass, 1 a check failed, 2 a variant is missing or has no results
(never a pass).  The sabotage is to point --mode at the base itself:
`--mode off` must fail check 1 (the cut costs about half the score).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import matrix  # noqa: E402

FAMILIES = {"img": ("img_", "imgwrong_", "imgstill_"), "skel": ("right_", "wrong_", "still_")}


def score(out, label, variant):
    t = matrix.summarize(os.path.join(out, label, variant))
    if not t or not t["songs"] or t["failed"] or not t["n"]:
        return None
    return t["score"]


def check(out, mode, base, family, cut, min_frac, margin, say=print):
    right, wrong, still = FAMILIES[family]
    need = {
        "full": (base, right + "full"),
        "cut": (mode, right + cut),
        "wrong_full": (base, wrong + "full"),
        "wrong_cut": (mode, wrong + cut),
        "still_full": (base, still + "full"),
        "still_cut": (mode, still + cut),
    }
    s = {}
    for k, (lab, var) in need.items():
        s[k] = score(out, lab, var)
        if s[k] is None:
            say(f"MISSING {lab}/{var}: no complete result -- not a pass")
            return 2
    ok = True
    frac = s["cut"] / s["full"] if s["full"] else 0.0
    good = frac >= min_frac
    ok &= good
    say(f"{'PASS' if good else 'FAIL'} right {cut} under {mode}: {s['cut']} = {frac:.3f} of "
        f"uncut under {base} ({s['full']}), need >= {min_frac}")
    for ctl in ("wrong", "still"):
        limit = s[ctl + "_full"] + margin * s["full"]
        good = s[ctl + "_cut"] <= limit
        ok &= good
        say(f"{'PASS' if good else 'FAIL'} {ctl} {cut} under {mode}: {s[ctl + '_cut']}, limit "
            f"{int(limit)} (= uncut under {base} {s[ctl + '_full']} + {margin} x {s['full']})")
    return 0 if ok else 1


def selftest():
    import json
    import tempfile
    root = tempfile.mkdtemp(prefix="pose_cutoff_check_")

    def put(label, variant, final):
        d = os.path.join(root, label, variant, "macarena")
        os.makedirs(d, exist_ok=True)
        json.dump({"results": True, "final_score": final}, open(os.path.join(d, "result.json"), "w"))
        open(os.path.join(d, "engine.log"), "w").write(
            "DC3 MOVE_RATING p=0 measure=4 move=x rating=move_awesome lastfrac=0.8 score=1\n")

    for v, n in (("img_full", 1000), ("imgwrong_full", 50), ("imgstill_full", 20)):
        put("off", v, n)
    put("off", "img_knees", 450)
    put("off", "imgwrong_knees", 10)
    put("off", "imgstill_knees", 10)
    put("est", "img_knees", 900)
    put("est", "imgwrong_knees", 60)
    put("est", "imgstill_knees", 25)
    quiet = lambda *_: None  # noqa: E731
    cases = [
        ("estimate passes", dict(mode="est"), 0),
        ("the base itself fails check 1", dict(mode="off"), 1),
        ("a tight margin fails check 2", dict(mode="est", margin=0.001), 1),
        ("a missing variant is not a pass", dict(mode="est", cut="midshin"), 2),
    ]
    bad = 0
    for name, kw, want in cases:
        args = dict(out=root, mode="est", base="off", family="img", cut="knees", min_frac=0.75,
                    margin=0.02)
        args.update(kw)
        got = check(say=quiet, **args)
        print(f"{'ok ' if got == want else 'BAD'} {name}: exit {got}, want {want}")
        bad += got != want
    return 1 if bad else 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--mode", default="kinematic")
    ap.add_argument("--base", default="off")
    ap.add_argument("--family", choices=sorted(FAMILIES), default="img")
    ap.add_argument("--cut", default="knees")
    ap.add_argument("--min-frac", type=float, default=0.75)
    ap.add_argument("--margin", type=float, default=0.02)
    a = ap.parse_args()
    return check(a.out, a.mode, a.base, a.family, a.cut, a.min_frac, a.margin)


if __name__ == "__main__":
    sys.exit(main())
