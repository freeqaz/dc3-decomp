#!/usr/bin/env python3
"""Refuse a build tree in which OUR object and the TARGET object spell the same
file-scope variable with different linkage: bare `gFoo` on one side and
global-scope-mangled `?gFoo@@3<type>` on the other.

The class
---------
MSVC/Xenon names a file-scope `static` datum with its BARE identifier in the
COFF symbol table (storage class 3, STATIC): `gSystemConfig`, `sCompressDone`.
A datum with external linkage is mangled: `?gNumHeaps@@3HA` (storage class 2).
The dtk-split target objects take every name from `config/<v>/symbols.txt`, and
objdiff's `name_check` ruler -- this repo's ruler -- charges a relocation whose
two sides name different symbols.  So when symbols.txt spells a variable
`?gRndThread@@3PAXA` and our source declares `static HANDLE gRndThread`, every
access costs a row even though the variable, the section and the offset are
all right: `Rnd::Init` and `Rnd::Terminate` sat at 99.7 with nothing else left.

The MSVC linker map lists NO static data at all (its "Static symbols" section
is functions only), so for a static the symbols.txt spelling is hand-authored
and the compiler's own output is the only authority on what it can be: a
`static` is bare, a global is mangled.  The two spellings therefore encode a
LINKAGE CLAIM, and a disagreement is one of exactly two things:

  * the symbols.txt entry claims external linkage the map cannot confirm --
    a config defect; spell it bare (`gRndThread`).  This is what the two
    Rnd.cpp entries were: upstream corrected them to bare in `3dbdf45a3`
    (2026-03-13) and a merge resolution put the mangled spelling back;
  * the map DOES carry the mangled name, so the original really had external
    linkage and our `static` is a source bug; drop the `static`.

Either fix makes the names identical.  A rename pass over the object would
make the same rows disappear while leaving the defect in place, and would
forgive the second case outright -- which is why this is a CHECK and not the
seventh patcher.  Measured 2026-09-11 over 980 paired units: 786 bare statics
on our side, 370 `?x@@3` globals on the target side, 2 disagreements, both in
`system/rndobj/Rnd.obj`, 3 functions / 9 relocation sites.

Two enumerations, cross-checked
-------------------------------
A checker that reports "0 disagreements" over the subset its regex can see is
the failure this repo has already paid for once (obj_anon_ns_patcher, `@@`
anchor, 5 of 19 sites never scanned).  So the target side is enumerated
TWICE, by independent readers -- the COFF symbol table, and dtk's own `.s`
listing (`.obj <name>, global|local` directives) -- and any object whose two
enumerations disagree is REFUSED (exit 4) rather than scored.  Both
denominators print on every run.

Exit codes
----------
  0  every paired unit agrees            1  at least one disagreement (listed)
  3  no objects to check (refusal)       4  enumeration cross-check failed
  5  --selftest found a case that cannot fail

Usage:
    python3 scripts/verify_data_symbol_spelling.py --check
    python3 scripts/verify_data_symbol_spelling.py --check --unit system/rndobj/Rnd.obj
    python3 scripts/verify_data_symbol_spelling.py --selftest
"""
from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VERSION = os.environ.get("DC3_VERSION", "373307D9")

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3
IMAGE_SCN_CNT_CODE = 0x20
COFF_MACHINE_POWERPCBE = 0x1F2

EXIT_DISAGREEMENT = 1
EXIT_EMPTY_UNIVERSE = 3
EXIT_CROSS_CHECK = 4
EXIT_SELFTEST_VACUOUS = 5

#: A plain C identifier -- what MSVC emits for internal linkage (and for
#: `extern "C"`).  Compiler-internal names (`__savegprlr_20`, `$L123`,
#: `@comp.id`, `.text`) are excluded by the prefix tests in `is_bare_name`.
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
#: A global-scope C++ variable: `?<ident>@@3<type-encoding>`.  `@@2` would be a
#: class-static member and `?x@?A0x...@@3` an anonymous-namespace member; both
#: are a different class (the anon-ns patcher's) and deliberately not matched.
MANGLED_GLOBAL_RE = re.compile(r"^\?([A-Za-z_][A-Za-z0-9_]*)@@3(.+)$")
#: dtk placeholder shapes.  A target datum the splitter never named carries no
#: linkage claim, and objdiff's `name_check` exempts it too.
PLACEHOLDER_RE = re.compile(
    r"^(lbl|data|bss|rdata|jumptable|fn|code|vftable)_[0-9A-Fa-f_]+$")


def is_bare_name(name: str) -> bool:
    return (bool(IDENT_RE.match(name))
            and not name.startswith("__")
            and not PLACEHOLDER_RE.match(name))


# ── COFF reader ─────────────────────────────────────────────────────────────


class Symbol:
    __slots__ = ("index", "name", "value", "section", "type", "cls")

    def __init__(self, index, name, value, section, typ, cls):
        self.index, self.name, self.value = index, name, value
        self.section, self.type, self.cls = section, typ, cls

    @property
    def is_function(self) -> bool:
        return (self.type >> 4) == 2

    @property
    def is_defined(self) -> bool:
        return self.section > 0


class CoffObject:
    """Just enough of a COFF reader: section flags and the symbol table."""

    def __init__(self, data: bytes, path: str = "<memory>"):
        self.path = path
        if len(data) < 20:
            raise ValueError(f"{path}: not a COFF object (too short)")
        self.machine, nsec = struct.unpack_from("<HH", data, 0)
        symoff, nsym = struct.unpack_from("<II", data, 8)
        opt_hdr, = struct.unpack_from("<H", data, 16)
        stroff = symoff + nsym * 18
        if symoff == 0 or nsym == 0 or stroff + 4 > len(data):
            raise ValueError(f"{path}: no symbol table")
        strsz, = struct.unpack_from("<I", data, stroff)
        strtab = data[stroff:stroff + strsz]

        self.section_flags: list[int] = []
        off = 20 + opt_hdr
        for _ in range(nsec):
            flags, = struct.unpack_from("<I", data, off + 36)
            self.section_flags.append(flags)
            off += 40

        self.symbols: list[Symbol] = []
        i = 0
        while i < nsym:
            e = data[symoff + i * 18: symoff + (i + 1) * 18]
            if e[:4] == b"\0\0\0\0":
                so, = struct.unpack_from("<I", e, 4)
                name = strtab[so:].split(b"\0", 1)[0].decode("latin1")
            else:
                name = e[:8].rstrip(b"\0").decode("latin1")
            value, section, typ, cls, aux = struct.unpack_from("<IhHBB", e, 8)
            self.symbols.append(Symbol(i, name, value, section, typ, cls))
            i += 1 + aux
        # The header's own count is the parser's denominator: every entry was
        # visited (aux records are skipped BY the record that owns them).
        self.n_symbol_entries = nsym

    def data_symbols(self) -> list[Symbol]:
        """Defined, non-function symbols (a datum, whatever section it is in)."""
        return [s for s in self.symbols if s.is_defined and not s.is_function]


# ── the two enumerations ────────────────────────────────────────────────────


def classify(obj: CoffObject) -> tuple[dict, dict]:
    """(bare_by_ident, mangled_by_ident) over the object's DEFINED data.

    Undefined externals (section 0) are excluded on purpose: they are what the
    object *references*, not what it *spells* for its own data, and the class
    is about the definition site.  (`--check` still charges a bare/mangled
    disagreement where the target merely references the datum, see
    `find_disagreements`: the relocation is what objdiff scores.)
    """
    bare: dict[str, list[Symbol]] = defaultdict(list)
    mangled: dict[str, list[Symbol]] = defaultdict(list)
    for s in obj.data_symbols():
        m = MANGLED_GLOBAL_RE.match(s.name)
        if m:
            mangled[m.group(1)].append(s)
        elif is_bare_name(s.name):
            bare[s.name].append(s)
    return bare, mangled


def referenced_names(obj: CoffObject) -> dict[str, str]:
    """ident -> spelling for every name the object carries at all (defined or
    undefined).  The target side of a charged relocation is often a symbol the
    target object only REFERENCES (the datum lives in another unit's split
    range), so the disagreement test reads this and not `classify`."""
    out: dict[str, str] = {}
    for s in obj.symbols:
        m = MANGLED_GLOBAL_RE.match(s.name)
        if m:
            out.setdefault(m.group(1), s.name)
        elif is_bare_name(s.name) and not s.is_function:
            out.setdefault(s.name, s.name)
    return out


OBJ_DIRECTIVE_RE = re.compile(r'^\.obj\s+(?:"([^"]+)"|(\S+?)),\s*(global|local|weak)\s*$')


def listing_data_names(asm_text: str) -> set[str]:
    """Data symbol names dtk's `.s` listing declares with `.obj` -- the
    independent enumeration of the TARGET object's data symbols."""
    names = set()
    for line in asm_text.splitlines():
        m = OBJ_DIRECTIVE_RE.match(line.strip())
        if m:
            names.add(m.group(1) or m.group(2))
    return names


def cross_check(target: CoffObject, asm_text: str) -> tuple[set, set]:
    """Names one enumeration has and the other lacks: (coff_only, listing_only).

    Restricted to the two spellings the check reasons about, so a listing
    quirk on an unrelated placeholder cannot make a real disagreement
    unmeasurable.  Both sets empty == the parser saw what dtk wrote.
    """
    def keep(n):
        return bool(MANGLED_GLOBAL_RE.match(n)) or is_bare_name(n)
    coff = {s.name for s in target.data_symbols() if keep(s.name)}
    lst = {n for n in listing_data_names(asm_text) if keep(n)}
    return coff - lst, lst - coff


# ── the check ───────────────────────────────────────────────────────────────


class Disagreement:
    __slots__ = ("unit", "ident", "ours", "ours_cls", "theirs", "map_says")

    def __init__(self, unit, ident, ours, ours_cls, theirs, map_says):
        self.unit, self.ident = unit, ident
        self.ours, self.ours_cls, self.theirs = ours, ours_cls, theirs
        self.map_says = map_says

    def fix(self) -> str:
        if self.map_says == "mangled":
            return (f"the linker map carries {self.theirs}: the original had "
                    f"EXTERNAL linkage, so drop `static` from the definition "
                    f"of {self.ident} in the source")
        if self.map_says == "bare":
            return (f"the linker map carries {self.theirs}: the original had "
                    f"INTERNAL linkage, so make {self.ident} `static` in the "
                    f"source")
        if self.ours_cls == IMAGE_SYM_CLASS_STATIC:
            return (f"the linker map carries neither spelling (it lists no "
                    f"static data), so the symbols.txt entry `{self.theirs}` "
                    f"is hand-authored: spell it `{self.ident}` in "
                    f"config/{VERSION}/symbols.txt (MSVC's spelling for a "
                    f"static), or drop `static` in the source if you have "
                    f"evidence the original exported it")
        return (f"the linker map carries neither spelling: our definition "
                f"is a global ({self.ours}) but symbols.txt says `{self.theirs}`; "
                f"make {self.ident} `static` in the source, or respell the "
                f"symbols.txt entry `{self.ours}` if the original exported it")


def load_map_names(map_path: Path | None) -> set[str]:
    names: set[str] = set()
    if map_path is None or not map_path.exists():
        return names
    pat = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+[0-9a-fA-F]{8}")
    with open(map_path, errors="replace") as fh:
        for line in fh:
            m = pat.match(line)
            if m:
                names.add(m.group(1))
    return names


def find_disagreements(unit: str, ours: CoffObject, target: CoffObject,
                       map_names: set[str]) -> list[Disagreement]:
    bare, mangled = classify(ours)
    theirs = referenced_names(target)
    out = []
    for ident, syms in bare.items():
        t = theirs.get(ident)
        if t is not None and t != ident:            # ours bare, target ?x@@3
            for s in syms:
                out.append(Disagreement(
                    unit, ident, s.name, s.cls, t,
                    "mangled" if t in map_names else
                    "bare" if ident in map_names else "absent"))
    for ident, syms in mangled.items():
        t = theirs.get(ident)
        if t is not None and t == ident:            # ours ?x@@3, target bare
            for s in syms:
                out.append(Disagreement(
                    unit, ident, s.name, s.cls, t,
                    "bare" if ident in map_names else
                    "mangled" if s.name in map_names else "absent"))
    return out


def pairs(src_dir: Path, obj_dir: Path, only: str | None = None):
    """(unit_rel, our_path, target_path) for every unit with both objects."""
    out = []
    for p in sorted(src_dir.rglob("*.obj")):
        rel = p.relative_to(src_dir).as_posix()
        if only and rel != only.replace(os.sep, "/"):
            continue
        t = obj_dir / rel
        if t.exists():
            out.append((rel, p, t))
    return out


def run_check(src_dir: Path, obj_dir: Path, asm_dir: Path | None,
              map_path: Path | None, only: str | None = None,
              out=sys.stdout, err=sys.stderr) -> int:
    units = pairs(src_dir, obj_dir, only)
    if not units:
        print(f"REFUSE: no paired objects under {src_dir} / {obj_dir}"
              f"{' for unit ' + only if only else ''} -- 0 disagreements over "
              f"0 units is not a pass. Run a full `ninja` first.", file=err)
        return EXIT_EMPTY_UNIVERSE
    map_names = load_map_names(map_path)
    n_ours = n_theirs = n_listings = 0
    disagreements: list[Disagreement] = []
    xfail: list[tuple[str, set, set]] = []
    for rel, op, tp in units:
        try:
            ours = CoffObject(op.read_bytes(), str(op))
            target = CoffObject(tp.read_bytes(), str(tp))
        except ValueError as exc:
            print(f"REFUSE: {exc}", file=err)
            return EXIT_CROSS_CHECK
        bare, mangled = classify(ours)
        n_ours += sum(len(v) for v in bare.values()) + sum(len(v) for v in mangled.values())
        n_theirs += len(target.data_symbols())
        if asm_dir is not None:
            s_path = asm_dir / Path(rel).with_suffix(".s")
            if s_path.exists():
                n_listings += 1
                coff_only, lst_only = cross_check(target, s_path.read_text(errors="replace"))
                if coff_only or lst_only:
                    xfail.append((rel, coff_only, lst_only))
        disagreements += find_disagreements(rel, ours, target, map_names)

    print(f"[data-symbol-spelling] {len(units)} paired units; "
          f"{n_ours} bare/mangled data definitions on our side, "
          f"{n_theirs} data symbols on the target side; "
          f"{n_listings} target objects cross-checked against their .s listing"
          f"{'' if map_names else '; NO linker map (fix hints degrade)'}",
          file=out)

    if xfail:
        print("=" * 72, file=err)
        print("TARGET ENUMERATION CROSS-CHECK FAILED", file=err)
        print("=" * 72, file=err)
        print(f"{len(xfail)} object(s) where the COFF symbol table and dtk's "
              f".s listing disagree about which data symbols exist. The check "
              f"below would be scoring a subset, so it is REFUSED instead.",
              file=err)
        for rel, coff_only, lst_only in xfail[:20]:
            print(f"  {rel}", file=err)
            for n in sorted(coff_only)[:5]:
                print(f"    COFF only:    {n}", file=err)
            for n in sorted(lst_only)[:5]:
                print(f"    listing only: {n}", file=err)
        return EXIT_CROSS_CHECK

    if not disagreements:
        print(f"[data-symbol-spelling] OK: 0 bare-vs-mangled disagreements", file=out)
        return 0

    print("=" * 72, file=err)
    print("OUR OBJECTS AND THE TARGET SPELL A VARIABLE WITH DIFFERENT LINKAGE",
          file=err)
    print("=" * 72, file=err)
    print(f"{len(disagreements)} disagreement(s) over {len(units)} paired units. "
          f"Every relocation against each of these costs a `name_check` row "
          f"for a variable that is otherwise right.", file=err)
    for d in disagreements:
        print(f"\n  {d.unit}: {d.ident}", file=err)
        print(f"    ours:   {d.ours}  (storage class "
              f"{'STATIC' if d.ours_cls == IMAGE_SYM_CLASS_STATIC else 'EXTERNAL' if d.ours_cls == IMAGE_SYM_CLASS_EXTERNAL else d.ours_cls})",
              file=err)
        print(f"    target: {d.theirs}", file=err)
        print(f"    fix:    {d.fix()}", file=err)
    print("\nSee docs/decomp/patterns/relocation-names-are-unmetered.md, "
          "\"Bare vs mangled statics\".", file=err)
    return EXIT_DISAGREEMENT


# ── --selftest: synthetic objects, and every RED case must actually go red ──


def synth_obj(symbols: list[tuple[str, int, int, int, int]],
              sections: list[tuple[str, int]] = (
                  (".text", 0x60000020), (".bss", 0xC0000080), (".data", 0xC0000040))
              ) -> bytes:
    """A minimal PowerPC COFF object: sections (empty) + a symbol table.

    `symbols` are (name, value, section, type, storage_class).  Names longer
    than 8 bytes go through the string table, shorter ones sit inline -- both
    paths are exercised by the selftest on purpose.
    """
    nsec = len(sections)
    strtab = bytearray(b"\0\0\0\0")
    entries = bytearray()
    for name, value, section, typ, cls in symbols:
        nb = name.encode("ascii")
        if len(nb) <= 8:
            head = nb.ljust(8, b"\0")
        else:
            head = struct.pack("<II", 0, len(strtab))
            strtab += nb + b"\0"
        entries += head + struct.pack("<IhHBB", value, section, typ, cls, 0)
    struct.pack_into("<I", strtab, 0, len(strtab))
    hdr_len = 20 + 40 * nsec
    symoff = hdr_len
    hdr = struct.pack("<HHIIIHH", COFF_MACHINE_POWERPCBE, nsec, 0,
                      symoff, len(symbols), 0, 0)
    secs = b""
    for name, flags in sections:
        secs += name.encode().ljust(8, b"\0") + struct.pack(
            "<IIIIIIHHI", 0, 0, 0, 0, 0, 0, 0, 0, flags)
    return hdr + secs + bytes(entries) + bytes(strtab)


def _listing_for(target_syms) -> str:
    return "\n".join(f'.obj "{n}", global' for n, _v, sec, typ, _c in target_syms
                     if sec > 0 and (typ >> 4) != 2) + "\n"


def selftest() -> int:
    """Plant each defect in a temp tree and REQUIRE the check to go red for
    the pinned reason; then remove it and require green.  A case that cannot
    fail exits 5 ("vacuous") rather than passing."""
    import io
    results = []

    def run(label, ours_syms, target_syms, expect_rc, expect_text=None,
            listing=None, want_green_after=True):
        with tempfile.TemporaryDirectory(prefix="dss-selftest-") as td:
            root = Path(td)
            src, obj, asm = root / "src", root / "obj", root / "asm"
            for d in (src / "u", obj / "u", asm / "u"):
                d.mkdir(parents=True)
            (src / "u" / "x.obj").write_bytes(synth_obj(ours_syms))
            (obj / "u" / "x.obj").write_bytes(synth_obj(target_syms))
            (asm / "u" / "x.s").write_text(
                listing if listing is not None else _listing_for(target_syms))
            o, e = io.StringIO(), io.StringIO()
            rc = run_check(src, obj, asm, None, out=o, err=e)
            ok = rc == expect_rc and (expect_text is None or expect_text in e.getvalue())
            results.append((label, ok, rc, expect_rc, e.getvalue()))

    FN = ("?f@@YAXXZ", 0, 1, 0x20, 2)
    # 1. the real class: ours static/bare, target mangled  -> RED
    run("bare static vs ?x@@3 target (long name)",
        [FN, ("gRndThread", 0x10, 2, 0, 3)],
        [FN, ("?gRndThread@@3PAXA", 0, 3, 0, 2)],
        EXIT_DISAGREEMENT, "gRndThread")
    # 2. the same defect through the INLINE (<=8 byte) name path -> RED
    run("bare static vs ?x@@3 target (short name)",
        [FN, ("gFoo", 0x10, 2, 0, 3)],
        [FN, ("?gFoo@@3HA", 0, 3, 0, 2)],
        EXIT_DISAGREEMENT, "gFoo")
    # 3. the mirror: ours global/mangled, target bare -> RED
    run("?x@@3 ours vs bare target",
        [FN, ("?gNumHeaps@@3HA", 0x10, 2, 0, 2)],
        [FN, ("gNumHeaps", 0, 3, 0, 2)],
        EXIT_DISAGREEMENT, "gNumHeaps")
    # 4. target only REFERENCES the datum (section 0) -- still a charge -> RED
    run("target references ?x@@3 it does not define",
        [FN, ("gRndThread", 0x10, 2, 0, 3)],
        [FN, ("?gRndThread@@3PAXA", 0, 0, 0, 2)],
        EXIT_DISAGREEMENT, "gRndThread", listing="")
    # 5. agreement, both spellings -> GREEN
    run("both bare (static on both sides)",
        [FN, ("gUsingCD", 0x10, 2, 0, 3)], [FN, ("gUsingCD", 0, 3, 0, 2)], 0)
    run("both mangled (global on both sides)",
        [FN, ("?gNumHeaps@@3HA", 0x10, 2, 0, 2)],
        [FN, ("?gNumHeaps@@3HA", 0, 3, 0, 2)], 0)
    # 6. a FUNCTION with the bare name is not a datum -> GREEN
    run("bare function name is not a datum",
        [("memcpy", 0, 1, 0x20, 2)], [("?memcpy@@3HA", 0, 3, 0, 2)], 0)
    # 7. anon-namespace and class-static spellings are another class -> GREEN
    run("anon-ns / class-static spellings are out of scope",
        [FN, ("sThreadData", 0x10, 2, 0, 3), ("gX", 0x20, 2, 0, 3)],
        [FN, ("?sThreadData@?A0x1234abcd@@3HA", 0, 3, 0, 2),
         ("?gX@Cls@@2HA", 0, 3, 0, 2)], 0)
    # 8. dtk placeholder on the target carries no claim -> GREEN
    run("placeholder target name",
        [FN, ("gRev", 0x10, 2, 0, 3)], [FN, ("lbl_82F14008", 0, 3, 0, 2)], 0)
    # 9. the listing disagrees with the COFF table -> REFUSED, not scored
    run("cross-check: listing names a datum the COFF table lacks",
        [FN, ("gUsingCD", 0x10, 2, 0, 3)], [FN, ("gUsingCD", 0, 3, 0, 2)],
        EXIT_CROSS_CHECK, "CROSS-CHECK",
        listing='.obj "gUsingCD", global\n.obj "?gGhost@@3HA", global\n')
    run("cross-check: COFF table names a datum the listing lacks",
        [FN, ("gUsingCD", 0x10, 2, 0, 3)], [FN, ("gUsingCD", 0, 3, 0, 2)],
        EXIT_CROSS_CHECK, "CROSS-CHECK", listing="")
    # 10. empty universe is a refusal
    with tempfile.TemporaryDirectory(prefix="dss-selftest-") as td:
        root = Path(td)
        (root / "src").mkdir()
        (root / "obj").mkdir()
        e = io.StringIO()
        rc = run_check(root / "src", root / "obj", None, None,
                       out=io.StringIO(), err=e)
        results.append(("empty universe refused", rc == EXIT_EMPTY_UNIVERSE,
                        rc, EXIT_EMPTY_UNIVERSE, e.getvalue()))

    bad = 0
    n_red = 0
    for label, ok, rc, want, text in results:
        if want != 0:
            n_red += 1
        print(f"  [{'OK ' if ok else 'BAD'}] {label}  (exit {rc}, wanted {want})")
        if not ok:
            bad += 1
            print("        stderr was:\n" + "\n".join("        " + l for l in text.splitlines()[:12]))
    if n_red == 0:
        print("SELFTEST VACUOUS: no case required a failure")
        return EXIT_SELFTEST_VACUOUS
    print(f"selftest: {len(results) - bad}/{len(results)} cases as expected, "
          f"{n_red} of them required the check to FAIL")
    return 1 if bad else 0


# ── main ────────────────────────────────────────────────────────────────────


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", default=str(REPO))
    ap.add_argument("--src-dir", help="our objects (default build/<v>/src)")
    ap.add_argument("--obj-dir", help="target objects (default build/<v>/obj)")
    ap.add_argument("--asm-dir", help="dtk .s listings (default build/<v>/asm)")
    ap.add_argument("--map", help="MSVC linker map (default orig/<v>/ham_xbox_r.map)")
    ap.add_argument("--unit", help="check ONE unit (relative path, e.g. system/rndobj/Rnd.obj)")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.check:
        ap.error("give --check or --selftest")
    repo = Path(a.repo).resolve()
    build = repo / "build" / VERSION
    return run_check(
        Path(a.src_dir) if a.src_dir else build / "src",
        Path(a.obj_dir) if a.obj_dir else build / "obj",
        Path(a.asm_dir) if a.asm_dir else build / "asm",
        Path(a.map) if a.map else repo / "orig" / VERSION / "ham_xbox_r.map",
        only=a.unit)


if __name__ == "__main__":
    sys.exit(main())
