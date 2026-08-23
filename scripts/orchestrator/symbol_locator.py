"""Where does a symbol actually live? -- the answer a "Symbol not found" owes you.

`run_objdiff` used to answer a missing symbol like this::

    Failed: Symbol not found: ?GetNumSongs@Playlist@@QBAHXZ
    Did you mean:
      - `?GetNumSongs@Playlist@@QBAHXZ` (0.0%)

A byte-identical suggestion. Two separate defects produced it:

1. **Unit and symbol resolve separately.** The suggestion list came from
   `decomp.db`, which knows the symbol; the *diff* failed because the symbol is
   not in the OBJECT that was searched. Both statements were true, and printed
   together they read as a contradiction. The DB row for that symbol names unit
   ``default/system/rndobj/Text`` -- a `Playlist` method attributed to `RndText`
   -- so even the suggestion's own unit was an attribution artifact.

2. **Three very different failures shared one message.** "not in the TARGET
   object", "not in the BASE object" and "nowhere in this project" mean
   completely different things:

   * only in BASE  -> we compiled something the shipped binary does not define
     under that name (inlined away, ICF-folded under a sibling's name, or a
     helper the original never had). Usually a symbol-ATTRIBUTION artifact, not
     missing work.
   * only in TARGET -> real unimplemented work; the object we build has no such
     symbol yet.
   * in both, different unit -> just pass the right ``unit=``.
   * referenced but never defined -> an undefined external; the definition is in
     a unit that is not in this project, or the name is misspelled at the call
     site.
   * nowhere -> spelling, or the wrong repo (dc3/rb3/rb3-xenon share names).

The scan is two-stage and cheap (~0.15 s over 3,200 objects, 160 MB, warm):
a raw substring pass over every object -- which is a SUPERSET, since a
relocation to an undefined external also carries the name in the string table
-- then a COFF symbol-table parse of only the files that hit, to separate
*defined* from *merely referenced*.

Every result carries its denominator (`stats`), because "found nowhere" and
"scanned nothing" produce identical-looking output and one of them is a finding.
"""
from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

IMAGE_SYM_CLASS_EXTERNAL = 2


def _load_coffx():
    """Import the repo's COFF reader without taking ownership of it."""
    analysis = Path(__file__).resolve().parent.parent / "analysis"
    if str(analysis) not in sys.path:
        sys.path.insert(0, str(analysis))
    import coffx  # type: ignore

    return coffx


@dataclass
class SymbolLocation:
    symbol: str
    #: units whose TARGET object DEFINES the symbol
    target_units: list[str] = field(default_factory=list)
    #: units whose BASE object DEFINES the symbol
    base_units: list[str] = field(default_factory=list)
    #: units that only REFERENCE it (undefined external / relocation target)
    target_refs: list[str] = field(default_factory=list)
    base_refs: list[str] = field(default_factory=list)
    stats: dict = field(default_factory=dict)

    @property
    def found_anywhere(self) -> bool:
        return bool(self.target_units or self.base_units
                    or self.target_refs or self.base_refs)


def locate_symbol(project_dir, symbol: str) -> SymbolLocation:
    """Find every object in the project that defines or references `symbol`.

    Raises nothing; a project with no readable `objdiff.json` comes back with
    ``stats["units_declared"] == 0``, which the formatter renders as "I could
    not scan anything" rather than as "the symbol is absent".
    """
    project_dir = Path(project_dir)
    loc = SymbolLocation(symbol=symbol)
    stats = loc.stats
    stats.update(units_declared=0, objects_scanned=0, objects_missing=0,
                 objects_unreadable=0, bytes_scanned=0, name_hits=0)

    try:
        cfg = json.loads((project_dir / "objdiff.json").read_text())
    except (OSError, ValueError):
        return loc
    units = cfg.get("units") or []
    stats["units_declared"] = len(units)

    needle = symbol.encode("ascii", "replace")
    # Stage 1: raw substring pass. Superset -- COFF puts every long name,
    # defined or undefined, in the string table.
    hits: list[tuple[str, str, Path]] = []
    for u in units:
        name = u.get("name") or ""
        for side, key in (("target", "target_path"), ("base", "base_path")):
            rel = u.get(key)
            if not rel:
                continue
            p = project_dir / rel
            try:
                data = p.read_bytes()
            except OSError:
                if p.exists():
                    stats["objects_unreadable"] += 1
                else:
                    stats["objects_missing"] += 1
                continue
            stats["objects_scanned"] += 1
            stats["bytes_scanned"] += len(data)
            if needle in data:
                stats["name_hits"] += 1
                hits.append((side, name, p))

    if not hits:
        return loc

    # Stage 2: parse only the hits, and separate DEFINED from REFERENCED.
    coffx = _load_coffx()
    for side, name, p in hits:
        try:
            _secs, syms = coffx.read_coff(p.read_bytes())
        except Exception:
            _secs, syms = None, None
        if not syms:
            # A name hit we could not adjudicate is recorded as a reference,
            # never silently dropped: dropping it would shrink the denominator
            # and make an unreadable object look like an absent symbol.
            (loc.target_refs if side == "target" else loc.base_refs).append(name)
            continue
        defined = referenced = False
        for s in syms:
            if s.name != symbol:
                continue
            if s.sec > 0:
                defined = True
            elif s.sec == 0 and s.cls == IMAGE_SYM_CLASS_EXTERNAL:
                referenced = True
        if defined:
            (loc.target_units if side == "target" else loc.base_units).append(name)
        elif referenced:
            (loc.target_refs if side == "target" else loc.base_refs).append(name)

    for lst in (loc.target_units, loc.base_units, loc.target_refs, loc.base_refs):
        lst.sort()
    return loc


def _fmt_units(units: list[str], cap: int = 6) -> str:
    shown = ", ".join(f"`{u}`" for u in units[:cap])
    return shown + (f" (+{len(units) - cap} more)" if len(units) > cap else "")


def retry_hint(units: list[str], searched_unit: "str | None") -> str:
    """"Retry with unit=..." — never naming the unit that was just searched.

    Module-level and separately tested ON PURPOSE. Inside `format_not_found`
    the branch ordering already makes a self-suggestion unreachable, which
    makes an inline guard untestable: a probe that removes it still passes.
    (Measured — a first draft's "never suggests the query" test was vacuous for
    exactly that reason, and only a sabotage run exposed it.) Pulled out here,
    the contract is directly exercisable, so the check can fail.

    The defect it exists against is the whole reason this module exists::

        Failed: Symbol not found: ?GetNumSongs@Playlist@@QBAHXZ
        Did you mean:
          - `?GetNumSongs@Playlist@@QBAHXZ`

    Answering a question with the question is not an answer.
    """
    others = [u for u in units if u != searched_unit]
    return f" Retry with `unit=` one of: {_fmt_units(others)}." if others else ""


def format_not_found(loc: SymbolLocation, searched_unit: str | None) -> str:
    """Render the location as the message the failure actually earned.

    `searched_unit` is the unit objdiff was told to look in (``None`` when the
    whole project was searched). Naming it is half the fix: the old message
    never said WHERE it had looked, which is why a suggestion identical to the
    query looked like a tool bug rather than a wrong-unit answer.
    """
    st = loc.stats
    if not st.get("units_declared"):
        return ("Could not scan this project's objects (no readable "
                "`objdiff.json`), so this is NOT evidence the symbol is absent.")

    where = f"unit `{searched_unit}`" if searched_unit else "the whole project"
    lines = [f"**Searched:** {where} "
             f"({st['objects_scanned']} objects, {st['bytes_scanned'] / 1e6:.0f} MB; "
             f"{st['objects_missing']} declared objects missing from disk)."]

    if not loc.found_anywhere:
        lines.append(
            f"\n**`{loc.symbol}` is in NO object in this project** — not the target "
            f"side, not our build, not even as an undefined reference. That is a "
            f"spelling/mangling problem, or the wrong repo: dc3, rb3 and rb3-xenon "
            f"share symbol names, and `project_dir` selects a worktree of THIS "
            f"project only."
        )
        return "\n".join(lines)

    t, b = loc.target_units, loc.base_units
    t_set, b_set = set(t), set(b)
    both = sorted(t_set & b_set)

    # NEVER name the unit that was just searched as the place to retry. An
    # early draft of this very function did exactly that again:
    # `??_H@YAXPAXIHP6APAX0@Z@Z` is defined in `default/App`'s TARGET and in a
    # DIFFERENT unit's BASE, the target/base intersection was therefore empty,
    # and the `_fmt_units(both or t)` fallback printed "not in `default/App`.
    # Retry with `default/App`." Caught by a probe, not by review -- which is
    # why the branch structure below is now driven by whether the SEARCHED unit
    # has the symbol on each side, rather than by set intersection.
    def _retry(units: list[str]) -> str:
        return retry_hint(units, searched_unit)

    if searched_unit:
        in_t, in_b = searched_unit in t_set, searched_unit in b_set
    else:
        in_t = in_b = False

    if searched_unit and in_t and in_b:
        lines.append(
            f"\n**Both objects for `{searched_unit}` DO define it**, so this failure is "
            f"not about existence — objdiff refused for another reason (ambiguous "
            f"suffix match, an unpaired COMDAT selection, or a stale object). Re-run "
            f"with the fully-mangled name, and check the object's mtime."
        )
    elif searched_unit and in_t and not in_b:
        lines.append(
            f"\n**In the TARGET object for `{searched_unit}`, but NOT in our BASE "
            f"object for it.** That is unimplemented work in this TU: we compile no "
            f"such symbol here."
            + (f" Our build does define it in {_fmt_units(b)} — if that is the same "
               f"function, it landed in the wrong TU." if b else "")
        )
    elif searched_unit and in_b and not in_t:
        lines.append(
            f"\n**In our BASE object for `{searched_unit}`, but NOT in the TARGET "
            f"object for it.** Usually a symbol-ATTRIBUTION artifact rather than "
            f"missing work: the original inlined it, or the linker folded it (ICF) "
            f"under a sibling's name, so `config/<title>/symbols.txt` has no such "
            f"name to give the split object."
            + (f" The target does define it in {_fmt_units(t)}." if t else
               " No target object defines it anywhere.")
        )
    elif both:
        lines.append(
            f"\n**Defined on both sides** in {_fmt_units(both)}"
            + (f", but not in `{searched_unit}`." if searched_unit else ".")
            + _retry(both)
        )
    elif t and b:
        lines.append(
            f"\n**Defined on both sides but in DIFFERENT units** — target: "
            f"{_fmt_units(t)}; our build: {_fmt_units(b)}. objdiff pairs per unit, so "
            f"it can never diff this as-is. Common for compiler-generated COMDAT "
            f"helpers (`??_H`, `??_E`, MakeString templates), where every TU emits its "
            f"own copy and the linker folds them; for those the pairing is an artifact "
            f"and there is nothing to fix." + _retry(t)
        )
    elif b and not t:
        lines.append(
            f"\n**In our BASE build only** ({_fmt_units(b)}); **no TARGET object "
            f"defines it.** This is usually a symbol-ATTRIBUTION artifact rather "
            f"than missing work: the original inlined the function, or the linker "
            f"folded it (ICF) under a sibling's name, so `config/<title>/symbols.txt` "
            f"has no such name to give the split object. There may be nothing to fix."
            + (f" `unit={b[0]}` will confirm it: objdiff answers "
               f"*Symbol not found in target* there, which is the diagnosis, not a "
               f"different failure." if b else "")
        )
    elif t and not b:
        lines.append(
            f"\n**In the TARGET only** ({_fmt_units(t)}); **our build does not define "
            f"it.** This one IS unimplemented work — the object we compile has no such "
            f"symbol. Check the unit's source for a missing or differently-mangled "
            f"definition." + _retry(t)
        )

    if not t and not b:
        refs = sorted(set(loc.target_refs) | set(loc.base_refs))
        lines.append(
            f"\n**Referenced but never defined** in {_fmt_units(refs)} — an undefined "
            f"external. Every hit is a call site; the definition is outside this "
            f"project's objects, or the name is misspelled at the reference."
        )
    else:
        extra = sorted(set(loc.target_refs) | set(loc.base_refs))
        if extra:
            lines.append(f"\nAlso *referenced* (not defined) by "
                         f"{len(extra)} unit(s): {_fmt_units(extra, 4)}.")
    return "\n".join(lines)
