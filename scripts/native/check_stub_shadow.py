#!/usr/bin/env python3
"""check_stub_shadow — post-link gate for weak-stub shadowing in the native port.

Problem this exists to catch
----------------------------
`native/src/engine_stubs_generated.cpp` defines *weak* placeholder bodies for
symbols the native port has not recovered.  A `weak` stub can legally shadow a
real `weak_odr` body (templates, `inline` functions, implicitly-instantiated
members): the linker takes whichever definition it sees first and is free to
pick the stub.  The native link additionally passes
`-Wl,--unresolved-symbols=ignore-all` and `-Wl,--allow-multiple-definition`, so
neither a missing symbol nor a duplicate one produces any diagnostic.

Consequence: a function can be fully recovered in the decomp, compiled into a
real object file, and still be *dead* in `dc3-native` because the stub won.  A
confirmed instance was `NuiTransformSkeletonToDepthImage` (stub `_stub_fn_111`),
where every consumer read uninitialised out-parameters.

What this script does
---------------------
1. Reads the *actual link line* for the target out of `build.ninja` (not a
   guess, not a source scan) and expands it into the full set of input objects
   and static-archive members.
2. `nm --defined-only` on the stub object gives the set of stub-provided
   symbols.
3. For every other link input, records which of those symbols it also defines.
4. Disassembles / inspects the **final linked executable** for each duplicated
   symbol and decides which body the linker actually chose, by fingerprinting
   the stub bodies (they reference `dc3::gStubTraceEnabled` /
   `dc3::StubTraceHit`, or are a bare `xor eax,eax; ret`) and by comparing
   `st_size` against every candidate definition.

Exit status
-----------
0  no stub symbol is bound in the final binary while a real definition exists
1  at least one SHADOWED symbol (this is the gate failing)
2  usage / environment error

Run manually:

    python3 scripts/native/check_stub_shadow.py --build-dir native/build

`--json` emits a machine-readable report.  `--all` also lists stub symbols that
are the only definition (informational, never a failure).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass, field

STUB_OBJ_HINT = "engine_stubs_generated.cpp.o"

# Symbol types nm reports for a *definition* we care about.
CODE_TYPES = set("Tt")
DATA_TYPES = set("DdBbRrGgSs")
WEAK_TYPES = set("WwVv")


def die(msg: str) -> None:
    print(f"check_stub_shadow: {msg}", file=sys.stderr)
    sys.exit(2)


def run(cmd: list[str]) -> str:
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0 and not p.stdout:
        return ""
    return p.stdout


# ---------------------------------------------------------------------------
# 1. link inputs, straight out of build.ninja
# ---------------------------------------------------------------------------

def link_inputs(build_dir: str, target: str) -> tuple[list[str], list[str]]:
    """Return (object_files, archive_files) for `target`'s link edge."""
    ninja = os.path.join(build_dir, "build.ninja")
    if not os.path.exists(ninja):
        die(f"{ninja} not found — configure the native build first")
    src = open(ninja, encoding="utf-8", errors="replace").read()

    m = re.search(rf"^build {re.escape(target)}: (\S+) (.*?)(?=\n[^ ])", src,
                  re.M | re.S)
    if not m:
        die(f"no link edge for target {target!r} in {ninja}")
    body = m.group(2)
    # strip the "| implicit || order-only" tail — implicit deps are not inputs
    body = re.split(r"\s\|\|?\s", body)[0]
    inputs = [tok for tok in body.split() if tok]

    # The LINK_LIBRARIES variable of the edge carries external archives
    edge = src[m.start():]
    edge = edge[:edge.find("\nbuild ", 1) if edge.find("\nbuild ", 1) > 0 else len(edge)]
    libs = re.search(r"^\s+LINK_LIBRARIES = (.*)$", edge, re.M)
    if libs:
        for tok in shlex.split(libs.group(1)):
            if tok.endswith(".a"):
                inputs.append(tok)

    objs, archives = [], []
    for tok in inputs:
        path = tok if os.path.isabs(tok) else os.path.join(build_dir, tok)
        path = os.path.normpath(path)
        if tok.endswith(".o") and os.path.exists(path):
            objs.append(path)
        elif tok.endswith(".a") and os.path.exists(path):
            archives.append(path)
    return objs, archives


# ---------------------------------------------------------------------------
# 2/3. symbol tables
# ---------------------------------------------------------------------------

@dataclass
class Defn:
    where: str          # object path (or "archive(member)")
    sym_type: str       # nm letter
    size: int


@dataclass
class Sym:
    name: str
    stub: Defn
    reals: list[Defn] = field(default_factory=list)
    refs: list[str] = field(default_factory=list)   # objects that reference it


def nm_undefined(path: str) -> set[str]:
    """Names this object/archive references but does not define."""
    out = run(["nm", "--undefined-only", "--no-demangle", path])
    res = set()
    for line in out.splitlines():
        parts = line.split()
        if parts and parts[-2:-1] in (["U"], ["w"]) or (
                len(parts) >= 2 and parts[-2] in ("U", "w")):
            res.add(parts[-1])
    return res


def nm_defined(path: str) -> list[tuple[str, str, int]]:
    """[(name, type_letter, size)] for defined symbols in an object/archive."""
    out = run(["nm", "-S", "--defined-only", "--no-demangle", path])
    res = []
    cur_member = ""
    for line in out.splitlines():
        if line.endswith(":") and not line.startswith(" "):
            cur_member = line[:-1]
            continue
        parts = line.split()
        if len(parts) == 3:              # addr type name
            _, t, name = parts
            size = 0
        elif len(parts) == 4:            # addr size type name
            _, sz, t, name = parts
            try:
                size = int(sz, 16)
            except ValueError:
                size = 0
        else:
            continue
        # Local symbols (lowercase nm letters) cannot participate in cross-TU
        # shadowing at all — `.L.str`, static functions, etc.  Skipping them
        # removes ~99% of the raw symbol volume and all of the false pairs.
        if t.islower():
            continue
        res.append((name, t, size, cur_member))
    return res


def collect(build_dir: str, target: str, verbose: bool):
    objs, archives = link_inputs(build_dir, target)
    stub_objs = [o for o in objs if o.endswith(STUB_OBJ_HINT)]
    if not stub_objs:
        die(f"stub object ({STUB_OBJ_HINT}) is not in {target}'s link line")

    syms: dict[str, Sym] = {}
    for so in stub_objs:
        for name, t, size, _ in nm_defined(so):
            if t in "Uun":
                continue
            syms[name] = Sym(name, Defn(so, t, size))

    if verbose:
        print(f"  link inputs: {len(objs)} objects, {len(archives)} archives",
              file=sys.stderr)
        print(f"  stub object defines {len(syms)} symbols", file=sys.stderr)

    for path in objs + archives:
        if path in stub_objs:
            continue
        for name, t, size, member in nm_defined(path):
            s = syms.get(name)
            if s is None or t in "Uun":
                continue
            where = f"{path}({member})" if member else path
            s.reals.append(Defn(where, t, size))
        # Arm 2: who *references* the stub symbol?  A stub that is the only
        # definition AND is referenced is a live call into a return-0 body —
        # that is the class NuiTransformSkeletonToDepthImage and
        # (anonymous namespace)::YUVtoRGB belonged to, and it is invisible to
        # the duplicate-definition test above.
        for name in nm_undefined(path):
            s = syms.get(name)
            if s is not None:
                s.refs.append(path)
    return syms, objs, archives


# ---------------------------------------------------------------------------
# 4. which body did the linker actually pick?
# ---------------------------------------------------------------------------

STUB_MARKERS = ("gStubTraceEnabled", "StubTraceHit", "_ZN3dc313StubTraceHitEPKc",
                "_ZN3dc317gStubTraceEnabledE")


def binary_symbols(binary: str) -> dict[str, tuple[int, int, str]]:
    """name -> (addr, size, type) for defined symbols in the linked binary."""
    out = run(["nm", "-S", "--defined-only", "--no-demangle", binary])
    res = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            addr, size, t, name = parts
            try:
                res[name] = (int(addr, 16), int(size, 16), t)
            except ValueError:
                pass
        elif len(parts) == 3:
            addr, t, name = parts
            try:
                res[name] = (int(addr, 16), 0, t)
            except ValueError:
                pass
    return res


def disasm(binary: str, addr: int, size: int) -> str:
    size = max(size, 16)
    return run(["objdump", "-d", "--no-show-raw-insn",
                f"--start-address=0x{addr:x}",
                f"--stop-address=0x{addr + size:x}", binary])


def looks_like_stub(text: str) -> bool:
    # Only instruction lines. objdump prints the symbol's own name in the
    # `0000... <name>:` label line, so scanning the whole listing made every
    # function whose *name* contains a marker look like a stub -- the --self-test
    # NEGATIVE case (dc3::StubTraceHit itself) caught exactly that.
    body = [l.split("\t", 1)[-1].strip()
            for l in text.splitlines() if re.match(r"^\s+[0-9a-f]+:", l)]
    body = [b for b in body if b and not b.startswith("nop")]
    if not body:
        return False
    body = [re.sub(r"\s*#.*$", "", b).strip() for b in body]   # drop comments

    # A stub body is *entirely* the HX_STUB_TRACE preamble plus a zero return:
    #     lea gStubTraceEnabled(%rip),%rax ; cmpb $1,(%rax) ; jne .Lout
    #     push %rbp ; mov %rsp,%rbp ; lea "name"(%rip),%rdi
    #     call dc3::StubTraceHit ; pop %rbp
    #   .Lout: xor %eax,%eax ; ret
    # Requiring EVERY instruction to be stub-shaped -- rather than "some
    # instruction mentions a marker" -- is what keeps a real function that
    # happens to call StubTraceHit from being misread as a stub. The --self-test
    # NEGATIVE case pins that.
    ALLOWED = {"xor", "ret", "retq", "push", "pop", "mov", "leave", "lea",
               "cmpb", "jne", "je", "jmp", "endbr64"}
    calls_only_trace = True
    for b in body:
        op = b.split()[0]
        if op == "call" or op == "callq":
            if "StubTraceHit" not in b:
                calls_only_trace = False
            continue
        if op not in ALLOWED:
            return False
        if op in ("mov", "push", "pop") and "%rbp" not in b and "%rsp" not in b:
            return False
    if not calls_only_trace:
        return False
    # ...and the return value must be a hard zero (or void).
    return any(re.match(r"^xor\s+%(e|r)ax,%(e|r)ax$", b) for b in body) or \
        all(b.split()[0] in ("ret", "retq", "push", "pop", "mov", "endbr64",
                             "leave") for b in body)


def verdict(binary: str, sym: Sym, bsyms) -> tuple[str, str]:
    """Returns (verdict, evidence)."""
    ent = bsyms.get(sym.name)
    if ent is None:
        return "ABSENT", "symbol not present in the linked binary"
    addr, size, t = ent
    if t in DATA_TYPES or sym.stub.sym_type in DATA_TYPES:
        # data symbol: decide by size
        if sym.stub.size and size == sym.stub.size and \
           all(d.size != size for d in sym.reals):
            return "SHADOWED", f"data size 0x{size:x} matches stub, not real defs"
        return "OK", f"data size 0x{size:x}"
    text = disasm(binary, addr, size)
    if looks_like_stub(text):
        head = "; ".join(
            l.split("\t", 1)[-1].strip()
            for l in text.splitlines() if re.match(r"^\s+[0-9a-f]+:", l))[:200]
        return "SHADOWED", f"0x{addr:x} size 0x{size:x}: {head}"
    return "OK", f"0x{addr:x} size 0x{size:x} does not match the stub fingerprint"


# ---------------------------------------------------------------------------
# 5. the other half of the silence: --unresolved-symbols=ignore-all
# ---------------------------------------------------------------------------
#
# The native link passes -Wl,--unresolved-symbols=ignore-all, so a symbol that
# nothing defines does not fail the link either -- it becomes an ordinary
# undefined dynamic symbol, and the process dies with "symbol lookup error" the
# first time that code path executes.
#
# CORRECTION (2026-08-19): an earlier version of this comment said the symbol
# gets "a JUMP_SLOT relocation to 0" and that this is why the failure is
# deferred.  Both halves are wrong.  The zero readelf prints is `st_value`,
# which is zero for *every* undefined symbol including printf's, and the
# .got.plt slots hold PLT+6 -- the ordinary lazy-binding trampoline.  The
# deferral is just default lazy binding, nothing specific to this binary.  The
# check that actually settles it:
#
#     LD_BIND_NOW=1 ./dc3-native      # dies at startup, naming the first symbol
#
# Verified both directions on 2026-08-19: the pre-fix binary died immediately on
# `undefined symbol: _hypot`, and after _hypot was defined the same command died
# on `undefined symbol: BinkOpenTrack` instead.  So every name below is a real
# hole, and LD_BIND_NOW is the cheap way to prove any individual one still is.
#
# `ldd -r` enumerates the same set as relinking with
# --unresolved-symbols=report-all (verified 2026-08-19: both produced the same
# 31 symbols for dc3-native), and it takes seconds instead of a full relink, so
# it is the practical gate.
#
# The baseline below is the accepted set as of 2026-08-19, after the
# fix/kinect-camera-path recoveries took it from 31 to 24.  The gate fails on
# anything NOT in it; shrinking the baseline is the way to tighten the link.

UNRESOLVED_BASELINE = {
    # --- Bink video SDK (proprietary, not shipped) -----------------------
    "BinkDoFrame",
    "BinkDoFrameAsync",
    "BinkDoFrameAsyncWait",
    "BinkGetFrameBuffersInfo",
    "BinkGetSummary",
    "BinkOpenTrack",
    "BinkPause",
    "BinkRegisterFrameBuffers",
    "BinkSetSoundOnOff",
    "BinkSetVolume",
    "BinkShouldSkip",
    "BinkWait",
    "_ZN12BinkMovieSys18PlatformStoreCacheEPvj",
    "_ZN13BinkMovieImpl17PlatformCacheFileEPKc",
    # --- Kinect / Xbox SDK ----------------------------------------------
    "DmIsDebuggerPresent",
    "NuiIdentityEnroll",
    "NuiSkeletonGetNextFrame",
    "XNotifyCreateListener",
    "_Z23NuiTransformMatrixLevel9__vector4",
    # --- PPC intrinsics with no x86 lowering ------------------------------
    "__vmaddfp",
    "__vspltw",
    # --- Xbox controller HID back end (os/Joypad_Xbox.cpp, not in the native
    #     build; native input goes through native/src/platform/Joypad_Native.cpp
    #     instead).  Their only caller, JoypadPollCommon, has zero callers and is
    #     not address-taken in dc3-native, so these are unreachable rather than
    #     latent.  Recovering them would mean inventing behaviour for an XInput
    #     EEPROM ("breed data") write path that no native code drives.
    "ReadSingleJoypad",
    "requestBreedWrite",
    # --- toolchain mismatch, low-probability latent abort -----------------
    # NOT "dead code" -- see below.  libstdc++ 15 on this box does not export
    # _ZNKSt9type_infoeqERKS_ at all (`nm -D --defined-only
    # /usr/lib/libstdc++.so.6 | grep 9type_infoeq` is empty); the comparison is
    # normally inlined.  All four references come from libstdc++'s own
    # header-instantiated templates in HttpServer.cpp.o, not from DC3 code, and
    # they are NOT uniformly unreachable:
    #
    #   _Sp_counted_ptr_inplace<_NFA<regex_traits<char>>,...>::_M_get_deleter   0 callers
    #   _Sp_counted_ptr_inplace<httplib::detail::mmap,...>::_M_get_deleter      0 callers
    #   regex_traits<char>::transform_primary<const char*>                      4 callers
    #   regex_traits<char>::transform_primary<char*>                            4 callers
    #
    # The transform_primary pair is reached from std::__detail::
    # _BracketMatcher::_M_apply's outlined equivalence-class lambda, so a regex
    # containing an equivalence class ("[[=x=]]") would abort the process on
    # first use.  Quoting "_M_get_deleter has 0 call sites" as the whole
    # justification covers only 2 of the 4 references and overstates the case.
    #
    # It stays on this list anyway because it is still not a decomp gap: there
    # is no DC3 body to recover, and defining a member of std::type_info
    # ourselves is reserved-name UB ([namespace.std]).  The correct framing is
    # "toolchain mismatch we accept, with a low-probability latent abort".
    "_ZNKSt9type_infoeqERKS_",                                # std::type_info::operator==
    #
    # RESOLVED on fix/kinect-camera-path (2026-08-19), kept here as a record of
    # what left the list and why -- do NOT re-add without re-checking:
    #   _hypot, CDGetError, Hmx::Matrix4::Col3  -> hosted in native_link_glue.cpp
    #   RecursePatternInternal, LockStream, UnlockStream
    #                                           -> #ifndef HX_NATIVE guards that
    #                                              should never have been there
    #   createFilter                            -> EQEffect.cpp declared it
    #                                              extern "C"; the target symbol
    #                                              is C++-mangled
}


def check_unresolved(binary: str) -> list[str]:
    out = subprocess.run(["ldd", "-r", binary], capture_output=True, text=True)
    found = set()
    for line in (out.stdout + out.stderr).splitlines():
        m = re.search(r"undefined symbol:\s*(\S+)", line)
        if m:
            found.add(m.group(1))
    return sorted(found)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default="native/build")
    ap.add_argument("--target", default="dc3-native")
    ap.add_argument("--binary", default=None,
                    help="linked executable (default: <build-dir>/<target>)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--all", action="store_true",
                    help="also list stub symbols with no competing definition")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--skip-unresolved", action="store_true",
                    help="skip the `ldd -r` unresolved-symbol check")
    ap.add_argument("--self-test", action="store_true",
                    help="prove the stub fingerprint is not vacuous: it must "
                         "identify a body known to be a stub and reject one "
                         "known to be real, in the same binary")
    args = ap.parse_args()

    build_dir = os.path.abspath(args.build_dir)
    binary = args.binary or os.path.join(build_dir, args.target)
    if not os.path.exists(binary):
        die(f"{binary} not found — build {args.target} first")

    bsyms_pre = binary_symbols(binary)
    if args.self_test:
        # A gate whose detector always says "not a stub" would report zero
        # SHADOWED forever and look healthy. Pin both directions against bodies
        # whose nature is not in question.
        POSITIVE = "D3DCubeTexture_UnlockRect"       # a stub, by construction
        NEGATIVE = "_ZN3dc312StubTraceHitEPKc"       # a real body, by construction
        ok = True
        for name, want in ((POSITIVE, True), (NEGATIVE, False)):
            ent = bsyms_pre.get(name)
            if ent is None:
                print(f"self-test: {name} not in {binary} — cannot run")
                return 2
            got = looks_like_stub(disasm(binary, ent[0], ent[1]))
            flag = "PASS" if got == want else "FAIL"
            if got != want:
                ok = False
            print(f"self-test {flag}: looks_like_stub({name}) = {got}, "
                  f"expected {want}")
        return 0 if ok else 1

    syms, objs, archives = collect(build_dir, args.target, args.verbose)
    bsyms = bsyms_pre

    dup = {n: s for n, s in syms.items() if s.reals}
    report = {"target": args.target, "binary": binary,
              "n_link_objects": len(objs), "n_link_archives": len(archives),
              "n_stub_symbols": len(syms), "n_with_real_definition": len(dup),
              "shadowed": [], "ok": [], "absent": [], "solo": []}

    for name, s in sorted(dup.items()):
        v, ev = verdict(binary, s, bsyms)
        rec = {"symbol": name,
               "demangled": run(["c++filt", name]).strip() or name,
               "stub_type": s.stub.sym_type,
               "real_defs": [{"where": d.where, "type": d.sym_type,
                              "size": d.size} for d in s.reals],
               "evidence": ev}
        report[{"SHADOWED": "shadowed", "OK": "ok",
                "ABSENT": "absent"}[v]].append(rec)

    for name, s in sorted(syms.items()):
        if s.reals:
            continue
        report["solo"].append({
            "symbol": name,
            "demangled": run(["c++filt", name]).strip() or name,
            "stub_type": s.stub.sym_type,
            "n_referencing_objects": len(s.refs),
            "referencing_objects": [os.path.basename(p) for p in sorted(s.refs)][:12],
        })
    report["solo_live"] = [r for r in report["solo"] if r["n_referencing_objects"]]
    report["solo_dead"] = [r for r in report["solo"] if not r["n_referencing_objects"]]

    new_unresolved = []
    if not args.skip_unresolved:
        found = check_unresolved(binary)
        report["unresolved"] = found
        new_unresolved = [s for s in found if s not in UNRESOLVED_BASELINE]
        report["unresolved_new"] = new_unresolved
        report["unresolved_fixed"] = sorted(UNRESOLVED_BASELINE - set(found))

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"link inputs      : {len(objs)} objects, {len(archives)} archives")
        print(f"stub symbols     : {len(syms)}")
        print(f"  also defined elsewhere : {len(dup)}")
        print(f"  SHADOWED (stub won)    : {len(report['shadowed'])}")
        print(f"  OK (real body won)     : {len(report['ok'])}")
        print(f"  not in binary          : {len(report['absent'])}")
        for rec in report["shadowed"]:
            print(f"\n!! SHADOWED {rec['demangled']}")
            print(f"   mangled : {rec['symbol']}")
            print(f"   evidence: {rec['evidence']}")
            for d in rec["real_defs"]:
                print(f"   real def: [{d['type']}] size 0x{d['size']:x} {d['where']}")
        print(f"\nstub symbols that are the ONLY definition : {len(report['solo'])}")
        print(f"  LIVE   (referenced by a real object) : {len(report['solo_live'])}")
        print(f"  unused (nothing references them)     : {len(report['solo_dead'])}")
        if args.all:
            print("\n-- LIVE stubs: every call to these reaches a return-0 body.\n"
                  "   Not a gate failure (there is no real definition to prefer),\n"
                  "   but this is the worklist the YUVtoRGB / "
                  "NuiTransformSkeletonToDepthImage bugs came off.")
            for rec in report["solo_live"]:
                print(f"   [{rec['n_referencing_objects']:3d} refs] {rec['demangled']}")
                print(f"              {', '.join(rec['referencing_objects'])}")

    if not args.json and not args.skip_unresolved:
        print(f"\nunresolved at link (`ldd -r`) : {len(report['unresolved'])} "
              f"(baseline {len(UNRESOLVED_BASELINE)})")
        if report["unresolved_fixed"]:
            print("  RESOLVED since the baseline (shrink UNRESOLVED_BASELINE):")
            for s in report["unresolved_fixed"]:
                print(f"    {run(['c++filt', s]).strip() or s}")
        for s in new_unresolved:
            print(f"!! NEW UNRESOLVED {run(['c++filt', s]).strip() or s}")
            print(f"   mangled: {s}  -- calls to this reach a JUMP_SLOT "
                  f"relocated to 0")

    return 1 if (report["shadowed"] or new_unresolved) else 0


if __name__ == "__main__":
    sys.exit(main())
