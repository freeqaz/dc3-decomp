"""Macro-aware preprocessed-source splice cache for the permuter.

Background
----------
~45-65% of every variant compile is MWCC re-parsing the same ~900KB of headers.
The headers don't change between variants in a sweep — only the target
function body does. This module preprocesses the source ONCE per Scorer
session (``mwcceppc -E``), caches the macro-free ``.i`` text, and for each
variant splices the variant's function text into the cached ``.i`` and
compiles that directly. Skipping the preprocess stage roughly halves the
per-variant compile time on cache-cold variants.

The blocker: ``mwcceppc -E`` strips all ``#define``s. A function body that
references a macro (``MILO_ASSERT``, ``FOREACH``, ``RELEASE``, ``nullptr``,
object-like constants, ...) cannot be spliced raw into the macro-free ``.i``
— the macro identifier becomes undefined. So this module is **macro-aware**:

1. At init, collect every ``#define NAME`` identifier defined in ``src/**``
   headers (and the source ``.cpp`` itself).
2. Per variant: tokenize the variant's function region. If it contains ANY
   collected macro identifier as a whole-word token, the fast path is unsafe
   — return None so the caller falls back to a normal full compile.
3. If clean: locate the function in the cached ``.i`` (by qualified name +
   brace matching), replace its region with the variant's function text, and
   return spliced bytes for the caller to compile.

Correctness was verified empirically: compiling spliced text in a
``.cpp``-named file produces a BYTE-IDENTICAL ``.o`` versus the normal path
(md5 match). Use a ``.cpp``-named work file for the spliced output, NOT
``.i`` — the ``.i`` extension embeds a different filename in the debug info.

The whole feature is gated behind ``PERMUTER_PREPROCESS_CACHE=1`` and is
designed to be backwards-safe: any failure (preprocess fails, function not
found, macro present, mismatched function text) returns None and the caller
compiles the variant the normal way.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from functools import lru_cache
from pathlib import Path
from typing import Optional


def fast_path_enabled() -> bool:
    """Whether the preprocessed-splice fast path is enabled (env-gated, off by default)."""
    return os.environ.get("PERMUTER_PREPROCESS_CACHE", "").strip() in ("1", "true", "yes", "on")


# A C/C++ identifier token.
_IDENT_RE = re.compile(rb"[A-Za-z_][A-Za-z0-9_]*")

# Sentinel emitted by the liveness probe (see _build_probe_block).
_PROBE_PREFIX = "__PPC_MACRO_PROBE__"
_PROBE_RE = re.compile(r"__PPC_MACRO_PROBE__(\d+)\s+([01])")

# An object-like or function-like macro definition. Captures the NAME.
# Matches:  #define NAME ...   #define NAME(args) ...
_DEFINE_RE = re.compile(
    rb"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)",
    re.MULTILINE,
)


@lru_cache(maxsize=8)
def collect_macro_names(repo_root: Path) -> tuple[str, ...]:
    """Collect every ``#define NAME`` identifier defined under ``src/``.

    These are *candidate* macro names — a name appearing here is only a real
    risk if it is actually defined (live) in the target TU, which is resolved
    separately by the ``#ifdef`` liveness probe (many of these defines are
    behind inactive ``#if`` guards, e.g. zlib's ``#define const``). Cached per
    repo_root because the header set is stable within a session. Returned as a
    sorted tuple so probe indices are deterministic.
    """
    names: set[str] = set()
    src_dir = repo_root / "src"
    if not src_dir.is_dir():
        return ()

    for path in src_dir.rglob("*"):
        if path.suffix not in (".h", ".hpp", ".hxx", ".inl", ".i"):
            continue
        try:
            data = path.read_bytes()
        except OSError:
            continue
        for m in _DEFINE_RE.finditer(data):
            names.add(m.group(1).decode("ascii", errors="replace"))

    return tuple(sorted(names))


def _build_probe_block(candidate_names: tuple[str, ...]) -> str:
    """Build an ``#ifdef`` liveness-probe block to append to the source.

    For each candidate macro name we emit a marker that resolves to ``1`` if
    the name is ``#define``d at the end of the TU and ``0`` otherwise. Because
    the probe uses ``#ifdef`` (never expansion), it cannot trigger compile
    errors from function-like macros used without arguments. The whole block
    is appended after the real source so it sees the final macro state.
    """
    lines = ["", "#if 1"]
    for i, name in enumerate(candidate_names):
        lines.append(f"#ifdef {name}")
        lines.append(f"{_PROBE_PREFIX}{i} 1")
        lines.append("#else")
        lines.append(f"{_PROBE_PREFIX}{i} 0")
        lines.append("#endif")
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def _parse_probe_results(
    pp_text: str, candidate_names: tuple[str, ...]
) -> tuple[frozenset[str], int]:
    """Parse probe markers from preprocessed text.

    Returns (live_macro_names, probe_region_start) where probe_region_start is
    the byte offset of the first probe marker in ``pp_text`` (so the caller can
    truncate the probe block off the cached ``.i``). If no markers are found,
    returns (empty set, len(pp_text)).
    """
    live: set[str] = set()
    first_off = len(pp_text)
    for m in _PROBE_RE.finditer(pp_text):
        idx = int(m.group(1))
        if m.start() < first_off:
            first_off = m.start()
        if m.group(2) == "1" and 0 <= idx < len(candidate_names):
            live.add(candidate_names[idx])
    return frozenset(live), first_off


def _find_func_region(text: str, qualified_name: str) -> Optional[tuple[int, int]]:
    """Locate ``ClassName::Method`` (or a free function) definition in ``text``.

    Returns (start, end) byte offsets covering the whole definition including
    the matching closing brace, or None if not found / not a definition.

    Matching strategy:
      * Build a regex for the qualified name followed by an argument list and
        an opening brace (allowing const/throw qualifiers, member-init lists
        for constructors, and arbitrary whitespace between ``)`` and ``{``).
      * Brace-match from the opening ``{`` to find the end.

    The qualified name is matched at a word boundary so ``Foo::Bar`` does not
    accidentally match ``Foo::BarBaz``.
    """
    # Escape "::" and identifier chars; allow the C++ name verbatim.
    name_pat = re.escape(qualified_name)
    # Require a non-identifier char (or start) before the name so we don't
    # match a longer-name suffix. The name is followed by an arg list "(".
    pattern = re.compile(
        r"(?:(?<=[^A-Za-z0-9_])|^)" + name_pat + r"\s*\(",
    )

    for m in pattern.finditer(text):
        open_paren = m.end() - 1
        # Balance the parameter-list parens.
        depth = 0
        k = open_paren
        n = len(text)
        while k < n:
            c = text[k]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    k += 1
                    break
            k += 1
        else:
            continue
        # After the param list, skip qualifiers (const, throw(...), member
        # init list for ctors) until we hit the body brace or a ';' (decl).
        body_open = _scan_to_body_brace(text, k)
        if body_open is None:
            continue  # a declaration or unparseable — not the definition
        end = _match_brace(text, body_open)
        if end is None:
            continue
        return (m.start(), end)
    return None


def _scan_to_body_brace(text: str, pos: int) -> Optional[int]:
    """From just after a parameter list, find the function body's opening ``{``.

    Skips ``const``, ``volatile``, ``throw(...)``, and a constructor's
    ``: member(...), ...`` init list. Returns the index of ``{`` or None if a
    ``;`` (declaration) or end-of-text is reached first.
    """
    n = len(text)
    i = pos
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if c == "{":
            return i
        if c == ";":
            return None  # declaration, not a definition
        if c == "(":
            # throw(...) or part of an init-list expression — balance it.
            end = _match_paren(text, i)
            if end is None:
                return None
            i = end
            continue
        # const / volatile / throw / ':' init-list separators / identifiers
        i += 1
    return None


def _match_brace(text: str, open_idx: int) -> Optional[int]:
    """Return the index just past the ``}`` matching ``text[open_idx] == '{'``."""
    if text[open_idx] != "{":
        return None
    depth = 0
    n = len(text)
    i = open_idx
    while i < n:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return None


def _match_paren(text: str, open_idx: int) -> Optional[int]:
    """Return the index just past the ``)`` matching ``text[open_idx] == '('``."""
    if text[open_idx] != "(":
        return None
    depth = 0
    n = len(text)
    i = open_idx
    while i < n:
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return None


def region_has_macro(region_bytes: bytes, macro_names: frozenset[str]) -> bool:
    """True if any whole-word identifier token in ``region_bytes`` is a macro."""
    for m in _IDENT_RE.finditer(region_bytes):
        if m.group(0).decode("ascii", errors="replace") in macro_names:
            return True
    return False


# A qualified function name at the head of a definition: optional return type,
# then ``Class::Method`` (one or more ``::`` segments) or a free ``Func`` name,
# immediately followed by ``(``. Used to derive the locator name from the
# variant's own function text so we don't depend on demangling.
_DEF_NAME_RE = re.compile(
    r"""
    (?P<name>
        (?:~?[A-Za-z_]\w*)            # leading identifier (or destructor ~)
        (?:\s*::\s*~?(?:[A-Za-z_]\w*|operator\s*\S+))*  # ::Method segments
    )
    \s*\(
    """,
    re.VERBOSE,
)


def extract_definition_name(func_text: str) -> Optional[str]:
    """Best-effort extract the qualified name that opens a function definition.

    Scans the text up to the first ``(`` that begins an argument list and
    returns the identifier chain immediately before it (collapsing internal
    whitespace around ``::``). Returns None if nothing plausible is found.
    """
    # The function name is the first ``Name(`` in the text: a return type
    # never ends in ``(``, so the first identifier-chain immediately followed
    # by ``(`` is the definition's own name. Parameter types like
    # ``const Hmx::Color &col`` appear only AFTER that first ``(``.
    m = _DEF_NAME_RE.search(func_text)
    if m is None:
        return None
    name = re.sub(r"\s*::\s*", "::", m.group("name")).strip()
    return name or None


class PreprocessCache:
    """Per-session cache of preprocessed source for fast variant compiles.

    Construct once per Scorer, call :meth:`prepare_from_text` with the
    preprocessed baseline text and the baseline function's byte range, then
    call :meth:`splice` per variant to obtain spliced ``.cpp`` bytes — or None
    to signal the caller should fall back to a full compile.

    The target function's location in the preprocessed text is resolved ONCE
    at prepare time (the ``.i`` is constant across variants), so per-variant
    work is just a macro scan + a string concatenation.
    """

    def __init__(self, repo_root: Path, source_path: Path):
        self.repo_root = repo_root
        self.source_path = source_path
        self._pp_text: Optional[str] = None
        # Macros that are actually LIVE (defined) in this TU — the gate set.
        self._live_macros: Optional[frozenset[str]] = None
        self._pp_region: Optional[tuple[int, int]] = None  # (start, end) in _pp_text
        self._func_name: Optional[str] = None
        self._disabled = False
        # Stats
        self.fast_hits = 0
        self.fallbacks = 0

    @property
    def disabled(self) -> bool:
        return self._disabled

    def probe_source(self, baseline_source: bytes) -> bytes:
        """Return source bytes augmented with the macro-liveness probe block.

        The caller preprocesses THIS (instead of the raw source) so a single
        ``-E`` pass yields both the cached ``.i`` and the live-macro map.
        """
        candidates = collect_macro_names(self.repo_root)
        block = _build_probe_block(candidates)
        text = baseline_source.decode("utf-8", errors="surrogateescape")
        return (text + block).encode("utf-8", errors="surrogateescape")

    def prepare_from_text(
        self,
        pp_text: str,
        baseline_source: bytes,
        baseline_func_range: Optional[tuple[int, int]],
    ) -> bool:
        """Seed the cache from preprocessed (probe-augmented) text.

        ``pp_text`` must be the result of preprocessing ``probe_source(...)``.
        This method parses the live-macro map from the probe markers, strips
        the probe region off the cached ``.i``, derives the function's
        qualified name from the baseline function text, and locates it in the
        ``.i``. On any failure the cache disables itself and every
        :meth:`splice` returns None (full-compile fallback).
        """
        if self._disabled:
            return False
        if not pp_text or not baseline_func_range:
            self._disabled = True
            return False
        start, end = baseline_func_range
        if start < 0 or end <= start or end > len(baseline_source):
            self._disabled = True
            return False

        try:
            baseline_func = baseline_source[start:end].decode("utf-8", errors="strict")
        except UnicodeDecodeError:
            self._disabled = True
            return False

        name = extract_definition_name(baseline_func)
        if not name:
            self._disabled = True
            return False

        # Parse + strip the liveness-probe block.
        candidates = collect_macro_names(self.repo_root)
        live, probe_start = _parse_probe_results(pp_text, candidates)
        if candidates and not live:
            # Probe markers absent — preprocess didn't include the block.
            # Bail to fallback rather than risk an unsound gate.
            self._disabled = True
            return False
        clean_pp = pp_text[:probe_start]

        region = _find_func_region(clean_pp, name)
        if region is None:
            self._disabled = True
            return False

        # ``_find_func_region`` anchors the region at the qualified NAME (not
        # the return type). The spliced variant text is aligned to the same
        # anchor in :meth:`splice`, so the return type already present in the
        # ``.i`` is preserved and never duplicated.
        self._pp_text = clean_pp
        self._pp_region = region
        self._live_macros = live
        self._func_name = name
        return True

    def splice(
        self,
        variant_source: bytes,
        func_byte_range: Optional[tuple[int, int]],
    ) -> Optional[bytes]:
        """Return spliced ``.cpp`` bytes for a variant, or None to fall back.

        Falls back (returns None) when:
          * the cache is disabled / not prepared,
          * the variant has no ``func_byte_range`` (can't isolate the body),
          * the variant's function region references a macro identifier,
          * the variant function text is unparseable / not brace-balanced.
        """
        if (
            self._disabled
            or self._pp_text is None
            or self._live_macros is None
            or self._pp_region is None
        ):
            return None
        if func_byte_range is None:
            self.fallbacks += 1
            return None

        start, end = func_byte_range
        if start < 0 or end <= start or end > len(variant_source):
            self.fallbacks += 1
            return None

        variant_func = variant_source[start:end]

        # Macro-aware gate: a body referencing any LIVE macro (one that is
        # actually #define'd in this TU) can't be spliced into the macro-free
        # preprocessed text — the identifier would be undefined or wrong.
        if region_has_macro(variant_func, self._live_macros):
            self.fallbacks += 1
            return None

        try:
            func_text = variant_func.decode("utf-8", errors="strict")
        except UnicodeDecodeError:
            self.fallbacks += 1
            return None

        # Align the variant text to the SAME anchor as the cached `.i` region:
        # the qualified name (not the return type). This avoids duplicating the
        # return type — the `.i` region already starts at the name, so the
        # spliced text must too. ``_find_func_region`` returns name-anchored
        # offsets within the variant function text.
        if self._func_name:
            vregion = _find_func_region(func_text, self._func_name)
            if vregion is None:
                self.fallbacks += 1
                return None
            func_text = func_text[vregion[0]:vregion[1]]

        # Sanity: the variant function text must itself contain a brace-balanced
        # body. If the byte range was off, bail rather than emit garbage.
        if "{" not in func_text or _match_brace(func_text, func_text.index("{")) is None:
            self.fallbacks += 1
            return None

        pp_start, pp_end = self._pp_region
        spliced = self._pp_text[:pp_start] + func_text + self._pp_text[pp_end:]
        self.fast_hits += 1
        return spliced.encode("utf-8", errors="surrogateescape")


def derive_preprocess_command(
    compile_shell_cmd: str,
    compile_output_path: Optional[str],
    pp_out_file: Path,
) -> Optional[str]:
    """Build a ``-E`` preprocess command from a normal ``-c`` compile command.

    Transformations:
      * ``-c`` flag -> ``-E`` (preprocess only).
      * ``-o <dir-or-file>`` -> ``-o <pp_out_file>`` (a ``.cpp``-named file).
      * Drop ``-MMD`` (a ``.d`` from preprocess input is useless here).

    Returns the new command string, or None if the command doesn't look like
    a recognized ``-c`` compile.
    """
    cmd = compile_shell_cmd
    if " -c " not in cmd and not cmd.endswith(" -c"):
        return None

    # Redirect output to the preprocessed file.
    if compile_output_path:
        cmd = cmd.replace(f"-o {compile_output_path}", f"-o {pp_out_file}")
    # Swap -c for -E (preprocess only). Replace the first standalone -c token.
    cmd = re.sub(r"(?<!\S)-c(?=\s)", "-E", cmd, count=1)
    # Drop -MMD: dependency generation off the preprocessed input is useless.
    cmd = re.sub(r"(?<!\S)-MMD(?=\s|$)", "", cmd)
    return cmd
