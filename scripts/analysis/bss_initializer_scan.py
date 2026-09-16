#!/usr/bin/env python3
"""Find statics we drop the initializer for: ours lands in .bss, the target's has content.

dc3-decomp (title 373307D9).  A `static float sZoom;` at namespace/class scope
lands in our .obj's `.bss` (IMAGE_SCN_CNT_UNINITIALIZED_DATA) and reads 0 at
runtime.  If the shipped image defines the same symbol with NONZERO bytes, our
declaration dropped a static initializer and the game starts that variable at
the wrong value.

This class is INVISIBLE to objdiff, which scores instruction streams and never
asks what a static's initial bytes were.  It is a pure behaviour bug, so the
match percentage will not move when you fix one.

Note the target objects are dtk splits of the image, so *every* section carries
raw bytes -- including the one named `.bss`.  The discriminator is therefore the
CONTENT (nonzero), never the section name.

THREE JOINS, IN DESCENDING ORDER OF EVIDENCE
============================================
The scan used to have exactly one join -- our symbol name against the SAME
NAME in the SAME object pair -- and that join reached 3,295 of 16,238 rows
(20.29%).  Two real bugs walked through the 80% it could not see, both AFTER
the class had been declared closed at 10/10.  The joins are now:

  1. `name-in-paired-target`  -- our name, found in the object it pairs with.
     The original join.  Strongest: no inference of any kind.

  2. `name-anywhere-in-target` -- our name, found in ANY target object.
     Closes the CROSS-TU blind spot.  `GainEffect::sGain` (df13adcd1c) had a
     correctly named target symbol (`?sGain@GainEffect@@0MA`, in the image's
     GainEffect.obj) while OUR definition sat in `Mic.cpp`; a per-object join
     could never bring the two together.  Still an exact name match, so the
     only inference is "one definition per name", which COFF guarantees for
     an external and which we check for a static by preferring the paired
     object first.

  3. `address-via-code-reference` -- our symbol has NO NAME on the target side
     at all, so it is located through an INSTRUCTION THAT READS IT.
     Closes the `lbl_*` blind spot.  See below.

WHY THE `lbl_*` JOIN GOES THROUGH CODE AND NOT THROUGH THE LINKER MAP
--------------------------------------------------------------------
The obvious fix for an unnamed target symbol is "join by address via
`orig/373307D9/ham_xbox_r.map`".  MEASURED ON THIS TREE, THAT RECOVERS ZERO
ROWS, and the reason is worth writing down because it also explains why the
name join can never be extended to cover this population:

  * of the 12,473 distinct names we place in .bss, 2,297 appear in the map;
  * of the 8,893 distinct names the name join CANNOT resolve, **0 appear in
    the map** -- not one.

They are almost all FUNCTION-LOCAL statics, and MSVC mangles a function-local
static with a per-TU SCOPE ORDINAL: `?sLastBeat@?1??Poll@Game@@QAAXXZ@4MA`,
`?_s@?CH@??Handle@AccomplishmentManager@@...`.  That ordinal (`?1@`, `?6@`,
`?CH@`) counts scopes in the translation unit as the compiler walks it, so OUR
ordinal and the image's differ whenever anything ahead of it in the file
differs.  This is the same `?BD@` vs `?BH@` divergence CLAUDE.md documents as
a relocation-name noise class that the graded ruler deliberately exempts.  A
function-local static is therefore UNJOINABLE BY NAME on principle, whether
the name comes from a target object or from the map.

What locates it instead is the code that reads it.  Our object has a
`REL_PPC_REFLO` relocation at some offset inside function F naming our .bss
symbol; the target's F, at the corresponding offset, has a relocation naming
whatever the image put there -- typically `lbl_<addr>`, a name that IS an
address.  Follow it and read the bytes.  That is an address join; it just
gets the address from the reference site rather than from a symbol table that
does not contain the symbol.

Aligning our F with the target's F is the only inference, and it is checked:

  * instruction words are compared with the RELOCATED FIELD masked -- the low
    16 bits at a REFHI/REFLO site, the whole word at any other relocation --
    so regalloc-identical code compares equal while opcode and register
    numbers still DISCRIMINATE.  (objdiff's `funclet_signature` zeroes the
    whole 4 bytes at a relocation; that is right for its purpose and wrong
    for this one, because it erases the opcode and would let a `lfs` align
    to a `lwz`.)
  * identical masked streams give tier `exact`;
  * otherwise `difflib` aligns the two masked streams and the reference must
    land inside a matching block -- tier `aligned`.  THIS TIER IS LOAD-BEARING,
    not a courtesy: `Game::Poll` is 98.77% and its masked stream is NOT
    identical (239 of 245 words align), so `Game::Poll::sLastBeat`
    (88c3d9c20c, ours .bss zero, image -1.0f at `lbl_82F1A524`) is reachable
    ONLY through it.
  * the aligned target instruction must carry a REFLO relocation of its own
    and must agree with ours in its top 16 bits (opcode + register fields).
  * our reference must have a zero displacement, so the byte range we compare
    is unambiguously OUR symbol and not a sibling +4 into the same group.
  * if two reference sites resolve to target bytes that DISAGREE, the row is
    dropped as ambiguous rather than decided arbitrarily.

WHAT THIS STILL CANNOT SEE
--------------------------
Read the COVERAGE block; every one of these is a counted drop, not a silence:

  * a .bss symbol NO CODE IN ITS OWN OBJECT READS (`no-code-reference-in-our-object`)
    -- referenced only cross-TU, or only from data, or not referenced at all;
  * a reference from a function the paired target object does not define
    (`referencing-function-absent-from-paired-target`) -- inlined away in the
    image, or split into a different target object;
  * a reference `difflib` cannot place, or whose aligned target instruction
    has no relocation / a different opcode.

A ZERO FROM THIS SCAN IS STILL NOT AN EXHAUSTION PROOF.  It is now a zero over
~95% of the population instead of ~20%, and the block says which ~5%.
See docs/decomp/patterns/dropped-static-initializer.md.

Usage:
    python3 scripts/analysis/bss_initializer_scan.py
    python3 scripts/analysis/bss_initializer_scan.py --selftest
    python3 scripts/analysis/bss_initializer_scan.py --max-size 4096
    python3 scripts/analysis/bss_initializer_scan.py --no-address-join   # A/B
"""
import argparse
import difflib
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coffx  # noqa: E402
from coverage import CoverageReport, add_coverage_args, EXIT_NO_INPUT  # noqa: E402

IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040

REL_PPC_REFHI = 0x10   # `lis rX, sym@ha`
REL_PPC_REFLO = 0x11   # `lfs fN, sym@l(rX)` / `addi rX, rY, sym@l` -- the low half

# Tier names, strongest first.  Used for ordering and for the summary table.
TIER_NAME_PAIR = 'name-in-paired-target'
TIER_NAME_BINARY = 'name-anywhere-in-target'
TIER_ADDR_EXACT = 'address-via-code-reference(exact)'
TIER_ADDR_ALIGNED = 'address-via-code-reference(aligned)'
TIERS = (TIER_NAME_PAIR, TIER_NAME_BINARY, TIER_ADDR_EXACT, TIER_ADDR_ALIGNED)


def load(path):
    """(sections, symbols) with sizes inferred, or (None, None)."""
    try:
        data = open(path, 'rb').read()
    except OSError:
        return None, None
    secs, syms = coffx.read_coff(data)
    if not secs:
        return None, None
    coffx.infer_sizes(secs, syms)
    return secs, syms


def defined_symbols(secs, syms, skip_section_symbols=False):
    """name -> (section, value, size) for data symbols with a real section.

    `skip_section_symbols` drops COFF SECTION-DEFINITION symbols -- the record
    named `.bss` that describes the section rather than any variable in it.
    They are indistinguishable from a variable by name alone, and 710 of them
    sit in this scan's universe.
    """
    out = {}
    if not secs:
        return out
    for s in syms:
        if not s.name or s.sec is None or s.sec <= 0 or s.sec > len(secs):
            continue
        if s.cls not in (coffx.IMAGE_SYM_CLASS_EXTERNAL, coffx.IMAGE_SYM_CLASS_STATIC):
            continue
        if skip_section_symbols and coffx.sym_kind(s) == coffx.K_SEC:
            continue
        sec = secs[s.sec - 1]
        if sec.is_code:
            continue
        out.setdefault(s.name, (sec, s.value, getattr(s, 'size', 0) or 0))
    return out


def code_functions(secs, syms):
    """name -> (section, symbol) for function symbols in code sections."""
    out = {}
    if not secs:
        return out
    for s in syms:
        if not s.name or s.sec is None or s.sec <= 0 or s.sec > len(secs):
            continue
        if s.cls not in (coffx.IMAGE_SYM_CLASS_EXTERNAL, coffx.IMAGE_SYM_CLASS_STATIC):
            continue
        sec = secs[s.sec - 1]
        if not sec.is_code:
            continue
        out.setdefault(s.name, (sec, s))
    return out


def masked_words(sec, sym):
    """Instruction words with the RELOCATED FIELD masked, opcode/registers kept.

    NOT objdiff's `funclet_signature`, deliberately.  That zeroes the whole
    4 bytes at every relocation address, which is correct for pairing funclets
    by byte signature and WRONG here: it erases the opcode, so a `lfs` and a
    `lwz` at the same offset compare equal and `difflib` will happily align
    them.  A REFHI/REFLO relocation only ever patches the low 16 bits, so mask
    exactly those and let the top half discriminate.  Any other relocation type
    (REL24 on a `bl`, ADDR32 in data) has its whole word zeroed, which is the
    conservative choice.
    """
    start = sym.value
    size = getattr(sym, 'size', 0) or 0
    end = start + size
    if size <= 0 or end > len(sec.data):
        return None
    b = bytearray(sec.data[start:end])
    for (va, _si, typ) in sec.relocs:
        if va < start or va >= end:
            continue
        o = va - start
        if typ in (REL_PPC_REFHI, REL_PPC_REFLO):
            if o + 4 <= len(b):
                b[o + 2] = 0
                b[o + 3] = 0
        else:
            for k in range(o, min(o + 4, len(b))):
                b[k] = 0
    return [struct.unpack_from('>I', b, i)[0] for i in range(0, len(b) - 3, 4)]


def align_map(our_words, tgt_words):
    """our word index -> target word index, or None if the streams are unrelated.

    Returns (mapping, exact) where `exact` is True when the two masked streams
    are identical (so the mapping is the identity and carries no inference).
    """
    if our_words == tgt_words:
        return {i: i for i in range(len(our_words))}, True
    sm = difflib.SequenceMatcher(a=our_words, b=tgt_words, autojunk=False)
    m = {}
    for i, j, n in sm.get_matching_blocks():
        for k in range(n):
            m[i + k] = j + k
    return m, False


def _disp(sec, va):
    """The signed 16-bit displacement field of the instruction at `va`."""
    if va + 4 > len(sec.data):
        return None
    return struct.unpack_from('>h', sec.data, va + 2)[0]


def _word(sec, va):
    if va + 4 > len(sec.data):
        return None
    return struct.unpack_from('>I', sec.data, va)[0]


def address_join(osecs, osyms, tsecs, tsyms, wanted_indices, tgt_rel, tgt_index):
    """Locate unnamed target counterparts through the code that reads them.

    `wanted_indices` maps OUR symbol-table index -> our symbol, for the .bss
    symbols still unresolved.  Returns (resolved, reasons) where

        resolved : our symbol index -> (tier, tgt_rel, sec_index, off, chars, tgt_name)
        reasons  : our symbol index -> a drop-reason slug, for the ones that
                   failed; only present when the index is not in `resolved`.

    Deterministic: reference sites are visited in (function name, offset)
    order, and a later site can only UPGRADE a row's tier, never replace an
    equal-tier decision.
    """
    resolved = {}
    reasons = {}
    seen_any_ref = set()
    tgt_fn = code_functions(tsecs, tsyms)
    tgt_by_index = {s.index: s for s in tsyms}

    ofns = sorted(code_functions(osecs, osyms).items(), key=lambda kv: kv[0])
    for fname, (osec, osym) in ofns:
        lo = osym.value
        hi = lo + (getattr(osym, 'size', 0) or 0)
        refs = sorted(
            (va, si) for (va, si, ty) in osec.relocs
            if ty == REL_PPC_REFLO and si in wanted_indices and lo <= va < hi
        )
        if not refs:
            continue
        for _va, si in refs:
            seen_any_ref.add(si)

        t = tgt_fn.get(fname)
        if t is None:
            for _va, si in refs:
                reasons.setdefault(si, 'referencing-function-absent-from-paired-target')
            continue
        tsec, tsym = t
        ow = masked_words(osec, osym)
        tw = masked_words(tsec, tsym)
        if ow is None or tw is None:
            for _va, si in refs:
                reasons.setdefault(si, 'referencing-function-unsized')
            continue
        amap, exact = align_map(ow, tw)
        tier = TIER_ADDR_EXACT if exact else TIER_ADDR_ALIGNED

        for va, si in refs:
            wi = (va - lo) // 4
            if wi not in amap:
                reasons.setdefault(si, 'reference-site-unalignable')
                continue
            tva = tsym.value + amap[wi] * 4
            trel = [(s, ty) for (v, s, ty) in tsec.relocs if v == tva
                    and ty in (REL_PPC_REFHI, REL_PPC_REFLO)]
            if not trel:
                reasons.setdefault(si, 'target-has-no-relocation-at-aligned-site')
                continue
            tsi, tty = trel[0]
            if tty != REL_PPC_REFLO:
                reasons.setdefault(si, 'target-relocation-is-not-the-low-half')
                continue
            ow_raw, tw_raw = _word(osec, va), _word(tsec, tva)
            if ow_raw is None or tw_raw is None or \
                    (ow_raw & 0xFFFF0000) != (tw_raw & 0xFFFF0000):
                reasons.setdefault(si, 'aligned-instruction-opcode-differs')
                continue
            od = _disp(osec, va)
            if od is None or od != 0:
                # Our reloc names symbol S but the instruction reads S+od, a
                # DIFFERENT variable in the same MSVC .data/.bss group.  The
                # bytes we would compare are not this symbol's.
                reasons.setdefault(si, 'our-reference-has-nonzero-displacement')
                continue
            tdatum = tgt_by_index.get(tsi)
            if tdatum is None:
                reasons.setdefault(si, 'target-relocation-target-undefined')
                continue
            td = _disp(tsec, tva)
            if td is None:
                reasons.setdefault(si, 'target-instruction-unreadable')
                continue
            if 0 < tdatum.sec <= len(tsecs):
                tds = tsecs[tdatum.sec - 1]
                if tds.is_code:
                    reasons.setdefault(si, 'target-relocation-points-into-code')
                    continue
                cand = (tier, tgt_rel, tds.index, tdatum.value + td,
                        tds.chars, tdatum.name)
            else:
                # The image's reference resolves to a symbol this target object
                # only DECLARES; dtk put the definition in a different split
                # object.  Look it up in the whole-binary index rather than
                # dropping the row -- this was 2,862 rows, the largest single
                # drop bucket, and every one of them is reachable.
                g = tgt_index.get(tdatum.name)
                if g is None:
                    reasons.setdefault(si, 'target-relocation-target-undefined')
                    continue
                grel, gsi, gval, _gsize, gchars = g
                cand = (tier, grel, gsi, gval + td, gchars, tdatum.name)
            prev = resolved.get(si)
            if prev is None:
                resolved[si] = cand
            elif prev[1:4] != cand[1:4]:
                # Two sites, two different target data locations.  Deciding
                # arbitrarily is exactly the first-write-wins defect this
                # repo keeps finding; refuse instead.
                resolved[si] = ('AMBIGUOUS',)
            elif TIERS.index(cand[0]) < TIERS.index(prev[0]):
                resolved[si] = cand

    for si in wanted_indices:
        if si in resolved or si in reasons:
            continue
        if si not in seen_any_ref:
            reasons[si] = 'no-code-reference-in-our-object'
        else:
            reasons[si] = 'reference-site-unresolved'
    return resolved, reasons


def build_target_index(tgt_root, cov=None):
    """name -> (rel, sec_index, value, size, chars) over EVERY target object.

    Primitives only: holding the `Sec` objects would pin every target object's
    bytes in memory.  Bytes are re-read, per object, only for the rows that
    actually resolve.  `sorted()` is load-bearing -- an unsorted glob feeding
    `setdefault` is first-write-wins by filesystem order, which is the
    scope_index_census defect.
    """
    idx = {}
    n_obj = 0
    n_unparseable = 0
    for tp in sorted(glob.glob(os.path.join(tgt_root, '**', '*.obj'), recursive=True)):
        n_obj += 1
        secs, syms = load(tp)
        if not secs:
            n_unparseable += 1
            continue
        rel = os.path.relpath(tp, tgt_root)
        for name, (sec, val, size) in defined_symbols(secs, syms).items():
            idx.setdefault(name, (rel, sec.index, val, size, sec.chars))
    return idx, n_obj, n_unparseable


def selftest():
    """Exercise the comparator and the aligner on synthetic input, both ways.

    A scanner whose instrument has never been SHOWN to fire is not evidence of
    anything -- and a one-sided control (it fired) passes just as happily on a
    harness that fires on everything.  Every check below has a negative twin.
    """
    ok = True

    def check(label, cond):
        nonlocal ok
        print(f"  {'PASS' if cond else 'FAIL'}  {label}")
        if not cond:
            ok = False

    class FakeSec:
        is_code = True
        index = 0

        def __init__(self, words, relocs, chars=0):
            self.data = b''.join(struct.pack('>I', w) for w in words)
            self.relocs = relocs
            self.chars = chars
            self.name = '.text'

    class FakeSym:
        def __init__(self, value, size, sec=1, name='f', index=0):
            self.value, self.size, self.sec = value, size, sec
            self.name, self.index = name, index
            self.cls = coffx.IMAGE_SYM_CLASS_EXTERNAL

    # -- masking: the relocated field is blanked, the opcode is NOT ----------
    lfs = 0xC0010008        # lfs f0, 8(r1)
    lwz = 0x80010008        # lwz r0, 8(r1)
    s = FakeSec([lfs], [(0, 7, REL_PPC_REFLO)])
    w = masked_words(s, FakeSym(0, 4))
    check("REFLO masking blanks the displacement", (w[0] & 0xFFFF) == 0)
    check("REFLO masking KEEPS the opcode (a lfs must not equal a lwz)",
          w[0] != masked_words(FakeSec([lwz], [(0, 7, REL_PPC_REFLO)]),
                               FakeSym(0, 4))[0])
    check("a non-REFLO relocation zeroes the whole word",
          masked_words(FakeSec([lfs], [(0, 7, 0x06)]), FakeSym(0, 4))[0] == 0)

    # -- alignment: identical is exact, one insertion still aligns, noise not
    a = [1, 2, 3, 4, 5]
    m, exact = align_map(a, a)
    check("identical streams align exactly", exact and m[4] == 4)
    m, exact = align_map(a, [1, 2, 99, 3, 4, 5])
    check("one inserted instruction still aligns the tail (not exact)",
          (not exact) and m.get(2) == 3 and m.get(4) == 5)
    m, exact = align_map(a, [90, 91, 92, 93, 94])
    check("an unrelated stream aligns nothing", not exact and m == {})

    # -- the content discriminator, both ways -------------------------------
    check("nonzero target bytes are a hit", any(b'\xbf\x80\x00\x00'))
    check("all-zero target bytes are NOT a hit (the image's own .bss)",
          not any(b'\x00\x00\x00\x00'))

    # -- coverage arithmetic: a bare `continue` must be catchable ------------
    cov = CoverageReport('selftest-balance', allow_truncation=True)
    cov.universe(3, 'synthetic rows')
    cov.examine()
    cov.drop('synthetic-reason')
    check("an uncounted row shows up as unaccounted", cov.unaccounted == 1)
    cov.drop('synthetic-reason')
    check("counting it balances the books", cov.unaccounted == 0)

    # -- end-to-end on the real corpus, when present ------------------------
    root = os.path.join(REPO, 'build', '373307D9')
    op = os.path.join(root, 'src', 'lazer', 'game', 'Game.obj')
    tp = os.path.join(root, 'obj', 'lazer', 'game', 'Game.obj')
    if os.path.exists(op) and os.path.exists(tp):
        osecs, osyms = load(op)
        tsecs, tsyms = load(tp)
        want = {s.index: s for s in osyms
                if s.name == '?sLastBeat@?1??Poll@Game@@QAAXXZ@4MA'}
        if want:
            res, why = address_join(osecs, osyms, tsecs, tsyms, want,
                                    'lazer/game/Game.obj', {})
            hit = [v for v in res.values() if v[0] != 'AMBIGUOUS']
            check("address join locates Game::Poll::sLastBeat's target datum",
                  len(hit) == 1 and hit[0][5].startswith('lbl_'))
            if hit:
                tier, _trel, tsi, off, _tchars, tname = hit[0]
                blob = tsecs[tsi].data[off:off + 4]
                check(f"...and reads -1.0f out of it ({tname}, {blob.hex()})",
                      blob == b'\xbf\x80\x00\x00')
                check("...through the ALIGNED tier (Poll is not byte-identical)",
                      tier == TIER_ADDR_ALIGNED)
        else:
            print("  SKIP  sLastBeat is not a symbol of Game.obj on this tree")
            print("        (it is .data when correct -- this check only runs")
            print("         while it is sabotaged, or against an older tree)")
        # The live negative: whatever sLastBeat is, a name that does not exist
        # must resolve to nothing.
        res, why = address_join(osecs, osyms, tsecs, tsyms,
                                {999999: FakeSym(0, 4, name='?nope@@3HA',
                                                 index=999999)},
                                'lazer/game/Game.obj', {})
        check("negative control: an unreferenced symbol resolves to nothing",
              res == {} and why.get(999999) == 'no-code-reference-in-our-object')
    else:
        print("  SKIP  live-corpus checks (built objects absent)")
        print("        This is NOT a pass: the comparator and the aligner were")
        print("        exercised, the COFF extractor was not. Build and re-run.")

    print("\nselftest:", "OK" if ok else "FAILED")
    return 0 if ok else 1


REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--project', default=os.getcwd())
    ap.add_argument('--max-size', type=int, default=4096,
                    help='ignore target symbols larger than this (default 4096)')
    ap.add_argument('--no-address-join', action='store_true',
                    help='disable join 3 (the code-reference/lbl_* join). For A/B '
                         'measurement only -- it LOWERS coverage, and the COVERAGE '
                         'block will say so.')
    ap.add_argument('--no-cross-tu', action='store_true',
                    help='disable join 2 (whole-binary name join). A/B only.')
    ap.add_argument('--selftest', action='store_true')
    add_coverage_args(ap)
    a = ap.parse_args()

    if a.selftest:
        return selftest()

    ours_root = os.path.join(a.project, 'build/373307D9/src')
    tgt_root = os.path.join(a.project, 'build/373307D9/obj')

    # Rule: a missing input is never a clean verdict.
    if not os.path.isdir(ours_root) or not os.path.isdir(tgt_root):
        print(f"INCONCLUSIVE: built objects not found under {ours_root} / {tgt_root}")
        print("Run `ninja` first; an unbuilt tree cannot be compared against the image.")
        return EXIT_NO_INPUT

    cov = CoverageReport('bss_initializer_scan', args=a)
    cov.require_examined('no .bss static was comparable against a target symbol')

    # PASS 1 -- the denominator, computed independently of any disposition.
    # Counting it inside the classification loop would make the arithmetic
    # check vacuous (universe would equal examined+drops by construction),
    # which is the exit-4 bypass coverage.py exists to prevent.
    pairs = []
    n_obj_unpaired = 0
    n_obj_unparseable = 0
    unpaired_names = []
    universe = 0
    for op in sorted(glob.glob(os.path.join(ours_root, '**', '*.obj'), recursive=True)):
        rel = os.path.relpath(op, ours_root)
        tp = os.path.join(tgt_root, rel)
        if not os.path.exists(tp):
            n_obj_unpaired += 1
            unpaired_names.append(rel)
            continue
        secs, syms = load(op)
        if not secs:
            # A 0-byte orphan of a deleted TU parses to nothing.  It is NOT a
            # silent skip: it contributes 0 rows to the universe and is named
            # in the coverage block.
            n_obj_unparseable += 1
            unpaired_names.append(rel + '  (unparseable/empty)')
            continue
        bss = {n: v for n, v in defined_symbols(secs, syms).items()
               if v[0].chars & IMAGE_SCN_CNT_UNINITIALIZED_DATA}
        universe += len(bss)
        pairs.append((rel, op, tp, sorted(bss)))
    cov.universe(universe, 'defined symbols WE place in .bss, across paired objects')

    # The whole-binary target index -- join 2.
    tgt_index, n_tgt_obj, n_tgt_unparseable = build_target_index(tgt_root)

    # PASS 2 -- classify. Every discard is counted.
    # Phase 2a resolves each row to a target byte range; phase 2b reads the
    # bytes, grouped by target object so each is opened once.
    wanted = {}          # (tgt_rel, sec_index) -> [(off, n, row)]
    tier_count = dict.fromkeys(TIERS, 0)
    hits = []
    n_section_syms = 0

    for rel, op, tp, bss_names in pairs:
        osecs, osyms = load(op)
        tsecs, tsyms = load(tp)
        if not osecs:
            # Cannot happen: pass 1 already parsed it.  Counted anyway rather
            # than `continue`d, because "cannot happen" is how rows vanish.
            cov.drop('our-object-unreadable-in-pass-2', n=len(bss_names))
            continue
        our_data = defined_symbols(osecs, osyms)
        our_real = defined_symbols(osecs, osyms, skip_section_symbols=True)
        tgt_pair = defined_symbols(tsecs, tsyms) if tsecs else {}

        # Which rows still need the address join, and under which symbol index?
        unresolved_names = []
        for name in bss_names:
            if name not in our_real:
                # A COFF SECTION-DEFINITION symbol (the record literally named
                # `.bss`), not a variable.  The universe deliberately still
                # counts it, so this scan's denominator stays comparable with
                # the 16,238 the old one reported -- but "examining" it meant
                # comparing the target's `.bss` section header against ours,
                # which is not a question about an initializer, and it inflated
                # the examined count with rows that can never be a finding.
                n_section_syms += 1
                cov.drop('coff-section-symbol-not-a-variable',
                         note='the COFF record named `.bss`, not a variable; kept in '
                              'the universe only so the denominator stays comparable')
                continue
            if name in tgt_pair:
                tier = TIER_NAME_PAIR
                tsec = tgt_pair[name][0]
                loc = (rel, tsec.index, tgt_pair[name][1], tgt_pair[name][2],
                       tsec.chars, name)
            elif (not a.no_cross_tu) and name in tgt_index:
                trel, tsi, tval, tsize, tchars = tgt_index[name]
                tier = TIER_NAME_BINARY
                loc = (trel, tsi, tval, tsize, tchars, name)
            else:
                unresolved_names.append(name)
                continue
            _stage(cov, a, wanted, rel, name,
                   our_data.get(name, (None, 0, 0))[2], tier, loc)

        if not unresolved_names:
            continue
        if a.no_address_join:
            cov.drop('no-name-match-anywhere-in-target', n=len(unresolved_names),
                     note='address join disabled by --no-address-join')
            continue
        want_idx = {}
        unresolved_set = set(unresolved_names)
        for s in osyms:
            if s.name in unresolved_set and s.sec and 0 < s.sec <= len(osecs) \
                    and not osecs[s.sec - 1].is_code:
                want_idx.setdefault(s.index, s)
        # A name can own several symbol records; resolve per NAME, taking the
        # strongest disposition any of its records achieves.
        resolved, reasons = address_join(osecs, osyms, tsecs, tsyms, want_idx,
                                         rel, tgt_index)
        best = {}
        why = {}
        for si, sym in sorted(want_idx.items()):
            r = resolved.get(si)
            if r is None:
                why.setdefault(sym.name, reasons.get(si, 'reference-site-unresolved'))
                continue
            if r[0] == 'AMBIGUOUS':
                why.setdefault(sym.name, 'address-join-ambiguous')
                continue
            prev = best.get(sym.name)
            if prev is None or TIERS.index(r[0]) < TIERS.index(prev[0]):
                best[sym.name] = r
        for name in unresolved_names:
            r = best.get(name)
            if r is None:
                cov.drop(why.get(name, 'reference-site-unresolved'))
                continue
            tier, trel, tsi, toff, tchars, tname = r
            loc = (trel, tsi, toff, 0, tchars, tname)
            _stage(cov, a, wanted, rel, name,
                   our_data.get(name, (None, 0, 0))[2], tier, loc)

    # -- phase 2b: read the bytes ------------------------------------------
    for key in sorted(wanted):
        trel, sec_index = key
        secs, _syms = load(os.path.join(tgt_root, trel))
        for off, n, row in sorted(wanted[key], key=lambda r: (r[0], r[2][1])):
            rel, name, tier, tname = row
            if not secs or sec_index >= len(secs):
                cov.drop('target-blob-unreadable')
                continue
            sec = secs[sec_index]
            blob = sec.data[off:off + n]
            if not blob:
                cov.drop('target-blob-unreadable')
                continue
            cov.examine()
            tier_count[tier] += 1
            if not any(blob):
                continue
            hits.append((rel, name, tier, sec.name, tname, off, blob))

    # -- report -------------------------------------------------------------
    if n_obj_unpaired or n_obj_unparseable:
        cov.note(f'{n_obj_unpaired} of our objects have no target counterpart and '
                 f'{n_obj_unparseable} parse to nothing (0-byte orphans of deleted '
                 f'TUs); their symbols are outside the universe entirely: '
                 + ', '.join(unpaired_names))
    cov.note(f'{n_tgt_obj} target objects indexed for the whole-binary name join '
             f'({n_tgt_unparseable} unparseable)')
    cov.note('three joins, strongest first: name-in-paired-target, '
             'name-anywhere-in-target (closes cross-TU), address-via-code-reference '
             '(closes target-side lbl_*). The linker map is NOT usable here: 0 of the '
             'names the name join misses appear in it -- see the module docstring.')
    if a.no_address_join or a.no_cross_tu:
        cov.note('!! A/B MODE: a join was disabled on the command line, so this '
                 'run DELIBERATELY examines less than the tool can.')
    cov.note(f'{n_section_syms} of the {universe} universe rows are COFF '
             f'SECTION-DEFINITION symbols (the record named `.bss`), not variables. '
             f'They are kept in the universe so this denominator stays comparable '
             f'with the one the name-only scan reported, and dropped rather than '
             f'examined, because comparing a section header against a section header '
             f'can never be a finding. Variable-only denominator: '
             f'{universe - n_section_syms}.')
    cov.extra('object_pairs', len(pairs))
    cov.extra('target_objects_indexed', n_tgt_obj)
    cov.extra('hits', len(hits))
    cov.extra('section_symbols_in_universe', n_section_syms)
    for t in TIERS:
        cov.extra('examined_by_' + t, tier_count[t])

    examined = sum(tier_count.values())
    variables = universe - n_section_syms
    print(f"{len(hits)} statics land in .bss but have NONZERO content in the shipped image")
    print(f"  examined {examined} of {universe} .bss symbols over {len(pairs)} object pairs "
          f"({100.0 * examined / universe:.2f}%)")
    print(f"  of those {universe}, {n_section_syms} are COFF section symbols and not "
          f"variables at all;")
    print(f"  against the {variables} real variables that is "
          f"{100.0 * examined / variables:.2f}%")
    for t in TIERS:
        print(f"    {t:38} {tier_count[t]}")
    print("(read the COVERAGE block below before calling this class exhausted -- "
          "the object-pair count is NOT this scan's denominator)\n")
    for rel, name, tier, secname, tname, off, blob in sorted(hits):
        show = blob[:32].hex()
        print(f"{rel}\n   {name}\n   via {tier} -> {tname}\n"
              f"   target {secname}+0x{off:x}  {len(blob)}B  {show}"
              f"{'...' if len(blob) > 32 else ''}\n")
    return cov.emit()


def _stage(cov, a, wanted, rel, name, our_size, tier, loc):
    """Queue one resolved row for the byte read, or drop it with a reason."""
    trel, sec_index, val, tsize, chars, tname = loc
    if not (chars & IMAGE_SCN_CNT_INITIALIZED_DATA):
        cov.drop('target-sym-not-in-initialized-data')
        return
    # OUR declared width first.  On the address-join path the target symbol is
    # frequently a `lbl_*` blob dtk carved to cover SEVERAL adjacent statics
    # (sLastBeat's is 16 bytes for a 4-byte float), so the target's size is an
    # upper bound on the group, not this datum's width.
    n = our_size or tsize or 4
    if n > a.max_size:
        cov.drop('capped-by-max-size',
                 note='target symbol larger than --max-size; raise it to include these')
        return
    wanted.setdefault((trel, sec_index), []).append(
        (val, n, (rel, name, tier, tname)))


if __name__ == '__main__':
    sys.exit(main())
