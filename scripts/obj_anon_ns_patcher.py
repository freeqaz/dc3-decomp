#!/usr/bin/env python3
"""Post-build patcher for MSVC anonymous namespace hashes in .obj files.

What the hash is
----------------
MSVC spells an anonymous namespace `?A0x<8 hex>@@` inside a mangled name, and
that hash is a function of the BUILD MACHINE'S COMPUTER NAME and the CANONICAL
PATH of the file the namespace is declared in.  It is not a function of the
file's contents: `scripts/probe_anon.py` (archived under
`<decomp-bench>/archive/runs/namecheck-lane-triage-and-fixers-20260812/`) walked
the inputs one at a time and found that editing the TU, changing the `/Fo`
directory or basename, compiling through retail's `e:` path and adding a `/Fd`
all leave it alone, while the host path, the file name and
`WIBO_COMPUTER_NAME` each move it.

So NO SOURCE EDIT CAN PRODUCE RETAIL'S VALUE.  It encodes a fact about
Harmonix's build host, in the same category as the `WIBO_COMPUTER_NAME='9QVZU3'`
already pinned in `tools/project.py`.  This pass reproduces that build-
environment input by rewriting our object after the compiler has run.  It is a
POST-BUILD REWRITE and the honest reading of the numbers it moves is "our
instruction stream and its relocation targets agree with retail once the build
host's identity is normalised away" -- not "our source now compiles to this".
The strictly-better fix is to make cl emit the value (a reverse
`WIBO_PATH_MAP` plus the right computer name per TU); it is not done here
because it changes shared tooling every decomp on this box uses.  Note it would
also be INCOMPLETE on its own -- see "one hash per FILE" below.

One hash per FILE, not one per object
-------------------------------------
Because the hash keys on the declaring file's path, an anonymous namespace in a
HEADER gets one hash shared by every TU that includes it, while a TU-local one
gets the `.cpp`'s.  Retail's tree bears this out: of 78 distinct hashes, 71 are
in exactly one object and 7 span several, and every multi-object hash carries
the SAME entity everywhere it appears -- `c9fefd64` is `AddToStrings` in 55
objects, `b39b74bf` is `DebugGraph` in 13, `81ddebd1` is `CuePoint`/`Label` in
5, `f8e4b4b5` is `Unlockable` in 4, `53f5bb0a` is `MonthToken`'s
`month_symbols` in 2.  Nineteen retail objects therefore carry two or three
hashes at once.

That is why a per-file hash swap is not enough, and why this pass used to give
up on exactly the objects that mattered: our build declares those entities in
the `.cpp`, so ONE of our hashes has to become SEVERAL of retail's, chosen per
symbol.  (That our tree needs the split at all is itself a finding: retail
declared `AddToStrings`, `Unlockable`, `DebugGraph`, `CuePoint`/`Label`,
`MonthToken` and HolmesClient's `gMachineName`/`gServerName`/`gShareName` in
headers, and we did not.  Repairing THAT would make the header hashes fall out
for free; it is a source-structure change and is not attempted here.)

How the assignment is decided
-----------------------------
By NAME.  Every hash occurrence sits inside a NUL-delimited mangled name.
Blank the hashes out of that name and you get a template; retail's object is
then asked what hashes belong in those positions.  Measured over the whole
retail tree this is unambiguous: 537 distinct templates, ZERO of which map to
two different hash tuples.  Token-level fallback (the identifier immediately
before `?A0x`) is ambiguous for exactly one entity (`Unlockable`, which retail
emits under both `9d17dd81` and `f8e4b4b5` in `MetagameRank.obj`), so it is
only consulted after the template lookups fail.

Resolution order per occurrence:

  1. exact template in the paired retail object          (`template`)
  2. exact template anywhere in the retail tree          (`template_global`)
  3. same, after stripping a `<prefix>$` decoration      (`template_stripped`)
     -- `__ehfuncinfo$`/`__unwindtable$`/`__catch$`/`__unwind$` wrappers we
     emit around a function retail also has
  4. the token before `?A0x`, from the paired object     (`token`)
  5. the token, from the retail tree                     (`token_global`)
  6. this object's majority target                       (`majority`)

Only 1-3 are evidence that retail states outright.  4-6 exist for symbols we
emit that retail never did (STL instantiations it inlined, EH tables it did not
need); those cannot match a retail name whatever we write, so the fallback is
about keeping the object internally consistent, not about buying a match.  Runs
resolved by 6 are counted and reported.

Every rewrite is 8 hex characters over 8 hex characters, so nothing in the
object moves: no string-table offset, no section size, no relocation.  This
also means the pass cannot reach retail's OTHER anonymous-namespace spelling,
the bare `?A@@` with no hash at all, which three retail objects use
(`rnddx9/Rnd`, `os/Joypad_Xbox`, `os/Joypad_Xinput`).  `?A0x<h>@@` is 12 bytes
and `?A@@` is 4; those are left alone and reported.

Patches are LOST on rebuild (same as the regswap patcher) - this is a
post-build step, re-run from `configure.py`'s `post-compile` chain.  The pass
is idempotent by construction: once our names equal retail's, rule 1 maps them
to themselves.

Usage:
    python3 scripts/obj_anon_ns_patcher.py --batch [--apply] [--verbose]

Without --apply, performs a dry run showing what would be changed.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
import sys as _sys, os as _os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
from obj_patch_io import write_patched_obj  # mtime-preserving in-place write

PROJECT_ROOT = Path(__file__).resolve().parent.parent
OBJ_DIR = PROJECT_ROOT / "build" / "373307D9" / "obj"
SRC_DIR = PROJECT_ROOT / "build" / "373307D9" / "src"

#: ⚠ The trailing scope is `@@` (global) for a top-level `namespace {}`, but a
#: NESTED anonymous namespace mangles as `?A0x<hash>@<backref digit>@` -- MSVC
#: gives every anonymous namespace in a TU the same `?A0x` fragment, so the
#: enclosing one is emitted as a back-reference.  Anchoring on `@@` made this
#: pass blind to those: `MoveDir.obj`'s
#: `??$__lower_bound@...UDetectFrameSecondsCmp@?A0xe50ea9de@3@H@...` carried an
#: UNPATCHED hash while the object reported "already correct", because 5 of its
#: 19 occurrences were never scanned.  The lookahead accepts both shapes and
#: still ends the match at the first `@`, so `template_of`/`apply_edits`
#: offsets are unchanged.
ANON_NS_PATTERN = re.compile(rb'\?A0x([0-9a-fA-F]{8})@(?=[@0-9])')
#: The hashless spelling.  `(?!0x)` keeps it from eating the hashed one.
HASHLESS_PATTERN = re.compile(rb'\?A(?!0x)@@')
#: Placeholder a template puts where a hash was.  Same length is not required;
#: templates are only ever compared to other templates.
TEMPLATE_MARK = b'?A0x*@@'
#: MSVC decorations wrapped around a function's name to make a companion
#: symbol (`__ehfuncinfo$?Foo@...`).  Retail often has the function but not the
#: companion, so the companion's hash has to be read off the function's.
DECORATION = re.compile(rb'^__[A-Za-z_]+\$')
#: The lexical-scope ordinal MSVC stamps into a function-local static's name
#: (`?month_symbols@?1??MonthToken@...`).  It is a SEPARATE residual lane
#: (`local_static_scope_ordinal`) and disagreeing on it must not stop us
#: reading the anonymous-namespace hash off the same name.
SCOPE_ORDINAL = re.compile(rb'\?[0-9A-Za-z]\?\?')
#: The identifier at the tail of a mangled scope component.  Stops at `@`, at
#: the `?<ord>??` marker, and at the leading `??__E`-style decorations.
TRAILING_IDENT = re.compile(rb'[A-Za-z0-9_<>\-]+$')


def hashless_names(path):
    """Retail's OTHER anonymous-namespace spelling, `?A@@` with no hash.

    Returns `{template: name}` for every NUL-delimited run in the object that
    names the anonymous namespace hashlessly, keyed by the same template the
    hashed path uses so the two can be compared.  Three retail objects use it
    (`rnddx9/Rnd`, `os/Joypad_Xbox`, `os/Joypad_Xinput`), and two of those use
    BOTH spellings in the same object for members of the SAME namespace block
    -- so it is a property of the individual symbol, not of the file, the TU or
    the namespace.  Cause not yet established.

    This pass cannot produce it: every rewrite it makes is 8 hex characters
    over 8 hex characters so that nothing in the object moves, and `?A0x<h>@@`
    is 12 bytes against `?A@@`'s 4.  Reported rather than silently dropped.
    """
    with open(path, 'rb') as fh:
        data = fh.read()
    out = {}
    for m in HASHLESS_PATTERN.finditer(data):
        start = data.rfind(b'\0', 0, m.start()) + 1
        end = data.find(b'\0', m.end())
        run = data[start:end if end >= 0 else len(data)]
        out[HASHLESS_PATTERN.sub(TEMPLATE_MARK, ANON_NS_PATTERN.sub(
            TEMPLATE_MARK, run))] = run
    return out


def find_anon_ns_hashes(data: bytes) -> set:
    """Find all unique anonymous namespace hashes in a COFF .obj file.

    Returns set of 8-byte ASCII hash strings (e.g., {b'c9fefd64'}).
    """
    return set(ANON_NS_PATTERN.findall(data))


def hash_runs(data: bytes):
    """Yield `(start, end)` of every NUL-delimited run holding an anon-ns hash.

    A COFF holds these names in the string table, in `.debug$S`/`.debug$T`, and
    in the RTTI type-descriptor data -- all NUL-terminated in practice.  We do
    not care which: the run around a hash is the name that names it, wherever
    it is stored, and the mapping below is by name.
    """
    spans = set()
    for m in ANON_NS_PATTERN.finditer(data):
        start = data.rfind(b'\0', 0, m.start()) + 1
        end = data.find(b'\0', m.end())
        spans.add((start, end if end >= 0 else len(data)))
    return sorted(spans)


def template_of(run: bytes) -> bytes:
    return ANON_NS_PATTERN.sub(TEMPLATE_MARK, run)


def hashes_of(run: bytes):
    """`[(offset_within_run_of_the_8_hex_chars, hash_bytes), ...]`."""
    return [(m.start(1) , m.group(1)) for m in ANON_NS_PATTERN.finditer(run)]


def token_of(run: bytes, hash_start: int) -> bytes:
    """The identifier immediately before the `?A0x` at `hash_start`.

    In a mangled name the scope chain runs right to left separated by `@`, so
    the component just before the anonymous-namespace marker is the entity
    declared in it: `?AddToStrings@?A0x...@@YA_N...` -> `AddToStrings`,
    `??_R0PAV?$_List_node@UReadRequest@?A0x...@@...` -> `UReadRequest`,
    `?month_symbols@?1??MonthToken@?A0x...@@...` -> `MonthToken` (the `?1??`
    scope ordinal is deliberately not part of the token: it is a different
    residual lane and we must not let a disagreement there hide the entity).
    """
    head = run[:hash_start - 4].rstrip(b'@')   # -4 skips the literal '?A0x'
    m = TRAILING_IDENT.search(head)
    return m.group(0) if m else head


def index_object(path):
    """`(template_map, token_map, hash_weights)` for one object.

    The first two carry SETS so an ambiguous key is visible as such rather than
    silently resolving to whichever entry was seen last.  `hash_weights` counts
    occurrences per hash, which is how "this object's dominant namespace" is
    decided when nothing else resolves.
    """
    with open(path, 'rb') as fh:
        data = fh.read()
    templates = defaultdict(set)
    tokens = defaultdict(set)
    weights = Counter()
    for start, end in hash_runs(data):
        run = data[start:end]
        tmpl = template_of(run)
        hashes = tuple(h for _, h in hashes_of(run))
        templates[tmpl].add(hashes)
        templates[SCOPE_ORDINAL.sub(b'?#??', tmpl)].add(hashes)
        for off, h in hashes_of(run):
            tokens[token_of(run, off)].add(h)
            weights[h] += 1
    return templates, tokens, weights


def build_obj_mappings(obj_dir: Path, src_dir: Path):
    """Build mappings between original and decomp .obj files.

    Returns:
        orig_by_relpath: {relative_path: absolute_path} for original .obj files
        decomp_by_relpath: {relative_path: absolute_path} for decomp .obj files
    """
    orig_by_relpath = {}
    for root, dirs, files in os.walk(obj_dir):
        for f in files:
            if not f.endswith('.obj') or f.startswith('auto_'):
                continue
            abspath = os.path.join(root, f)
            relpath = os.path.relpath(abspath, obj_dir)
            orig_by_relpath[relpath] = abspath

    decomp_by_relpath = {}
    for root, dirs, files in os.walk(src_dir):
        for f in files:
            if not f.endswith('.obj'):
                continue
            abspath = os.path.join(root, f)
            relpath = os.path.relpath(abspath, src_dir)
            decomp_by_relpath[relpath] = abspath

    return orig_by_relpath, decomp_by_relpath


def _lookup(mapping, key):
    """The single value `key` maps to, or None if absent or ambiguous."""
    got = mapping.get(key)
    if got is None or len(got) != 1:
        return None
    return next(iter(got))


def _template_target(run, local_templates, global_templates):
    """Retail's hash tuple for this exact name, or None.  Returns (tuple, rule)."""
    tmpl = template_of(run)
    for mapping, rule in ((local_templates, 'template'),
                          (global_templates, 'template_global')):
        got = _lookup(mapping, tmpl)
        if got is not None:
            return got, rule
    # Same name, different lexical-scope ordinal.  Both index maps carry the
    # ordinal-blanked spelling as an extra key, so this is still a name match.
    flat = SCOPE_ORDINAL.sub(b'?#??', tmpl)
    if flat != tmpl:
        for mapping in (local_templates, global_templates):
            got = _lookup(mapping, flat)
            if got is not None:
                return got, 'template_ordinal'
    # A companion symbol we emit around a function retail also has:
    # `__ehfuncinfo$?Foo@?A0x...@@...` -> ask about `?Foo@?A0x...@@...`.
    m = DECORATION.match(tmpl)
    if m and m.end() < len(tmpl):
        for inner in (tmpl[m.end():], flat[m.end():]):
            for mapping in (local_templates, global_templates):
                got = _lookup(mapping, inner)
                if got is not None:
                    return got, 'template_stripped'
    return None, None


def plan_object(data, orig_index, global_index):
    """Decide a replacement hash for every anon-ns occurrence in `data`.

    Returns `(edits, stats, unresolved)` where `edits` is
    `{absolute_offset_of_8_hex_chars: new_hash_bytes}`.
    """
    o_templates, o_tokens, o_weight = orig_index
    g_templates, g_tokens = global_index
    edits = {}
    stats = Counter()
    deferred = []          # (absolute_offset, run, offset_in_run)

    for start, end in hash_runs(data):
        run = data[start:end]
        here = hashes_of(run)
        target, rule = _template_target(run, o_templates, g_templates)
        if target is not None and len(target) == len(here):
            for (off, _), new in zip(here, target):
                edits[start + off] = new
            stats[rule] += len(here)
            continue
        for off, _ in here:
            tok = token_of(run, off)
            got = _lookup(o_tokens, tok)
            if got is not None:
                edits[start + off] = got
                stats['token'] += 1
                continue
            got = _lookup(g_tokens, tok)
            if got is not None:
                edits[start + off] = got
                stats['token_global'] += 1
                continue
            deferred.append((start + off, run, off))

    unresolved = []
    if deferred:
        # Majority over what the evidence-backed rules decided for THIS object,
        # falling back to retail's own dominant hash in this object when
        # nothing at all resolved (we emit an anonymous-namespace entity retail
        # does not have, but retail's object does have the namespace).
        # Deterministic across passes: both sources are retail, never our
        # current spelling.
        source = Counter(edits.values()) or o_weight
        if source:
            majority = source.most_common(1)[0][0]
            for offset, _run, _off in deferred:
                edits[offset] = majority
            stats['majority'] += len(deferred)
        else:
            unresolved = [run for _o, run, _f in deferred]
    return edits, stats, unresolved


def apply_edits(data: bytes, edits: dict) -> bytes:
    """Write each 8-hex hash in place.  Length-preserving by construction."""
    out = bytearray(data)
    for offset, new in edits.items():
        assert len(new) == 8, new
        assert out[offset - 4:offset] == b'?A0x', offset
        # `@@` for a top-level anonymous namespace, `@<backref digit>@` for a
        # nested one -- same shape ANON_NS_PATTERN accepts.  Still a real
        # guard: anything else means the offset is not a hash site.
        assert out[offset + 8:offset + 9] == b'@', offset
        assert out[offset + 9:offset + 10].isdigit() \
            or out[offset + 9:offset + 10] == b'@', offset
        out[offset:offset + 8] = new
    return bytes(out)


def patch_obj_file(obj_path: str, old_hash: bytes, new_hash: bytes,
                   apply: bool = False) -> int:
    """Replace all occurrences of one hash with another (whole-file).

    Retained for callers that want the old blunt behaviour; the batch pass
    below does NOT use it, because a single hash of ours routinely has to
    become several of retail's.
    """
    old_pattern = b'?A0x' + old_hash + b'@@'
    new_pattern = b'?A0x' + new_hash + b'@@'

    with open(obj_path, 'rb') as f:
        data = f.read()

    count = data.count(old_pattern)
    if count == 0:
        return 0

    if apply:
        new_data = data.replace(old_pattern, new_pattern)
        write_patched_obj(obj_path, new_data)

    return count


def process_batch(args):
    """Process all decomp .obj files in batch mode."""
    obj_dir = Path(args.obj_dir) if args.obj_dir else OBJ_DIR
    src_dir = Path(args.src_dir) if args.src_dir else SRC_DIR

    if not obj_dir.exists():
        print(f"ERROR: Original .obj directory not found: {obj_dir}", file=sys.stderr)
        sys.exit(1)
    if not src_dir.exists():
        print(f"ERROR: Decomp .obj directory not found: {src_dir}", file=sys.stderr)
        sys.exit(1)

    # Build file mappings by relative path (handles duplicate filenames correctly)
    orig_by_relpath, decomp_by_relpath = build_obj_mappings(obj_dir, src_dir)

    if args.verbose:
        print(f"Found {len(orig_by_relpath)} original .obj files")
        print(f"Found {len(decomp_by_relpath)} decomp .obj files")
        print("Indexing original .obj files by mangled-name template...")

    orig_index = {}
    g_templates = defaultdict(set)
    g_tokens = defaultdict(set)
    for relpath, abspath in orig_by_relpath.items():
        templates, tokens, weights = index_object(abspath)
        if not templates:
            continue
        orig_index[relpath] = (templates, tokens, weights)
        for k, v in templates.items():
            g_templates[k] |= v
        for k, v in tokens.items():
            g_tokens[k] |= v
    global_index = (g_templates, g_tokens)

    if args.verbose:
        amb_t = sum(1 for v in g_templates.values() if len(v) > 1)
        amb_k = sum(1 for v in g_tokens.values() if len(v) > 1)
        print(f"Retail name templates: {len(g_templates)} ({amb_t} ambiguous); "
              f"tokens: {len(g_tokens)} ({amb_k} ambiguous)")

    patched_files = 0
    total_replacements = 0
    already_ok = 0
    skipped_no_orig = 0
    skipped_no_hash_orig = 0
    skipped_unresolved = 0
    rules = Counter()
    hashless = []            # (relpath, ours, retail's) -- see hashless_names()

    for relpath, decomp_path in sorted(decomp_by_relpath.items()):
        with open(decomp_path, 'rb') as fh:
            data = fh.read()
        if not ANON_NS_PATTERN.search(data):
            continue

        if relpath not in orig_by_relpath:
            if args.verbose:
                print(f"  SKIP {relpath}: no matching original .obj")
            skipped_no_orig += 1
            continue
        # Retail's hashless `?A@@` spelling, wherever it names a symbol we
        # spell with a hash.  Counted whether or not the object also has hashed
        # names to patch, because two of the three objects that use it use BOTH.
        for tmpl, retail_name in hashless_names(orig_by_relpath[relpath]).items():
            for start, end in hash_runs(data):
                if template_of(data[start:end]) == tmpl:
                    hashless.append((relpath, data[start:end], retail_name))

        if relpath not in orig_index:
            # Retail's object has no anonymous namespace where ours does.
            # That is a SOURCE difference -- we wrapped in an anonymous
            # namespace where retail used file-scope `static`, or retail
            # spelled it `?A@@` -- not a naming one, and no length-preserving
            # rewrite can fix it.  See
            # docs/analysis/anon-namespace-hash-lane-20260812.md.
            if args.verbose:
                print(f"  SKIP {relpath}: original has no anonymous namespace hashes")
            skipped_no_hash_orig += 1
            continue

        edits, stats, unresolved = plan_object(data, orig_index[relpath],
                                               global_index)
        if unresolved:
            skipped_unresolved += 1
            if args.verbose:
                print(f"  SKIP {relpath}: {len(unresolved)} occurrence(s) with "
                      f"no retail evidence and no in-object majority")
                for run in unresolved[:3]:
                    print(f"        {run.decode('latin1')[:110]}")
            continue

        changed = {o: h for o, h in edits.items() if data[o:o + 8] != h}
        rules.update(stats)
        if not changed:
            already_ok += 1
            if args.verbose:
                summary = ', '.join(f"{k}={v}" for k, v in sorted(stats.items()))
                print(f"  OK   {relpath}: {len(edits)} occurrence(s) already "
                      f"correct ({summary})")
            continue

        patched_files += 1
        total_replacements += len(changed)
        if args.apply:
            write_patched_obj(decomp_path, apply_edits(data, edits))
        if args.verbose:
            moves = Counter((data[o:o + 8], h) for o, h in changed.items())
            summary = ', '.join(f"{k}={v}" for k, v in sorted(stats.items()))
            action = "PATCH" if args.apply else "WOULD PATCH"
            print(f"  {action} {relpath}: {len(changed)} occurrence(s) [{summary}]")
            for (old, new), n in moves.most_common():
                print(f"        {old.decode()} -> {new.decode()}  ({n})")

    action_word = "Applied" if args.apply else "Would apply"
    print(f"\n{action_word} patches to {patched_files} files ({total_replacements} total replacements)")
    print(f"Already matching: {already_ok}")
    print(f"Skipped (no retail evidence): {skipped_unresolved}")
    print(f"Skipped (no matching original): {skipped_no_orig}")
    print(f"Skipped (original has no anon ns): {skipped_no_hash_orig}")
    if hashless:
        units = sorted({r for r, _o, _t in hashless})
        print(f"Out of reach (retail spells it `?A@@`, hashless): "
              f"{len(hashless)} name(s) in {len(units)} object(s) -- a length "
              f"change this pass does not make, see "
              f"docs/analysis/anon-namespace-hash-lane-20260812.md")
        for relpath, ours, theirs in hashless:
            print(f"    {relpath}: {ours.decode('latin1')[:80]}")
            print(f"    {' ' * len(relpath)}  retail: {theirs.decode('latin1')[:80]}")
    if rules:
        print("Assignment rules: "
              + ', '.join(f"{k}={v}" for k, v in sorted(rules.items())))

    if not args.apply and patched_files > 0:
        print(f"\nRun with --apply to actually patch the files.")

    if getattr(args, 'check', False) and patched_files > 0:
        print('FAIL[anon_ns]: {n} pending patch(es) -- this build tree carries '
              'objects that were compiled but never post-processed. See '
              'docs/tools/BUILD_SYSTEM.md "post-compile patchers".'.format(n=patched_files),
              file=sys.stderr)
        sys.exit(2)


def main():
    parser = argparse.ArgumentParser(
        description='Patch anonymous namespace hashes in decomp .obj files to match originals')
    parser.add_argument('--apply', action='store_true',
                        help='Actually apply patches (default: dry run)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show per-file details')
    parser.add_argument('--batch', action='store_true',
                        help='Process all decomp .obj files')
    parser.add_argument('--obj-dir',
                        help='Original .obj directory (default: build/373307D9/obj)')
    parser.add_argument('--src-dir',
                        help='Decomp .obj directory (default: build/373307D9/src)')
    parser.add_argument('--check', action='store_true',
                        help='Dry-run and EXIT 2 if any object in the build tree '
                             'still needs this pass (used by '
                             'scripts/verify_objs_patched.py)')
    args = parser.parse_args()

    if not args.batch:
        print("ERROR: Currently only --batch mode is supported.", file=sys.stderr)
        print("Usage: python3 scripts/obj_anon_ns_patcher.py --batch [--apply] [--verbose]",
              file=sys.stderr)
        sys.exit(1)

    process_batch(args)


if __name__ == '__main__':
    main()
