"""Instruction attribution — connect objdiff mismatches to source lines.

Parses MSVC /FAs assembly listings to build a mapping from compiled
instruction offsets to source file/line, then joins that mapping against
objdiff's instruction-level diff data to produce *attributed* mismatches
and aggregated mismatch regions.

This is Synthesis Engine Phase 1 — see docs/plans/synthesis-engine/ROADMAP.md.

Usage:
    from scripts.permuter.attribution import (
        parse_asm_listing,
        attribute_mismatches,
        aggregate_regions,
    )

    listing = parse_asm_listing(asm_text)
    attributed = attribute_mismatches(listing, diff_instructions)
    regions = aggregate_regions(attributed, source_lines)
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field


# ---------------------------------------------------------------------------
# Dataclasses
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class AsmInstruction:
    """A single instruction from a /FAs assembly listing."""

    offset: int | None  # Byte offset in .text (None if not parseable)
    opcode: str  # e.g., "cmpw", "subf.", "beq"
    operands: list[str]  # e.g., ["r3", "r4"]
    raw_text: str  # Full line from listing


@dataclass(frozen=True)
class AsmEntry:
    """A source-attributed block of instructions from a /FAs listing."""

    source_file: str  # e.g., "src/system/os/BlockMgr.cpp"
    source_line: int  # Line number in source
    source_text: str  # Source text from listing comment
    instructions: tuple[AsmInstruction, ...]


@dataclass
class AsmListing:
    """Parsed /FAs assembly listing for a single function."""

    function_name: str
    entries: list[AsmEntry]
    prologue_helper: str | None = None  # e.g., "__savegprlr_24"
    callee_saved_count: int = 0

    def instruction_count(self) -> int:
        """Total instructions across all entries."""
        return sum(len(e.instructions) for e in self.entries)

    def source_line_for_offset(self, offset: int) -> AsmEntry | None:
        """Find the source entry containing an instruction at the given offset."""
        for entry in self.entries:
            for instr in entry.instructions:
                if instr.offset == offset:
                    return entry
        return None

    def source_line_for_index(self, index: int) -> AsmEntry | None:
        """Find the source entry containing the Nth instruction (0-based)."""
        i = 0
        for entry in self.entries:
            for _ in entry.instructions:
                if i == index:
                    return entry
                i += 1
        return None

    def all_instructions(self) -> list[tuple[AsmEntry, AsmInstruction]]:
        """Flat list of (entry, instruction) pairs in order."""
        result = []
        for entry in self.entries:
            for instr in entry.instructions:
                result.append((entry, instr))
        return result


@dataclass(frozen=True)
class AttributedMismatch:
    """An objdiff mismatch attributed to a specific source line."""

    instruction_index: int  # Index in instruction stream
    target_opcode: str
    base_opcode: str
    mismatch_type: str  # "opcode", "register", "missing", "extra"
    source_file: str | None  # None if attribution failed
    source_line: int | None
    source_text: str | None
    confidence: float  # 0.0-1.0


@dataclass
class MismatchRegion:
    """Aggregated mismatch region spanning contiguous source lines."""

    source_file: str
    start_line: int
    end_line: int  # inclusive
    source_lines: list[str]  # The actual source text
    mismatches: list[AttributedMismatch]
    dominant_type: str  # Most common mismatch_type in region
    total_instructions: int  # Instructions generated from this region
    matched_instructions: int  # How many of those match
    unattributed_count: int = 0  # Mismatches that couldn't be attributed

    @property
    def match_ratio(self) -> float:
        """Fraction of instructions in this region that match."""
        if self.total_instructions == 0:
            return 1.0
        return self.matched_instructions / self.total_instructions

    @property
    def mismatch_count(self) -> int:
        return len(self.mismatches)

    @property
    def impact(self) -> int:
        """Number of mismatched instructions — primary budget allocation signal."""
        return len(self.mismatches)


# ---------------------------------------------------------------------------
# /FAs Listing Parser
# ---------------------------------------------------------------------------

# Source line comment: "; 42   :     int a = GetValue();"
_SOURCE_LINE_RE = re.compile(r"^;\s*(\d+)\s*:\s*(.*)$")

# File reference: "; File c:\...\src\system\os\BlockMgr.cpp" or just path
_FILE_REF_RE = re.compile(r"^;\s*(?:File\s+)?(.+\.\w+)\s*$", re.IGNORECASE)

# Function boundary markers
_PROC_RE = re.compile(r"^(\?\S+)\s+PROC\s+NEAR", re.IGNORECASE)
_ENDP_RE = re.compile(r"^(\?\S+)\s+ENDP", re.IGNORECASE)

# Instruction line with optional hex offset:
#   "00008  81230000   lwz     r9,0(r3)"
#   or just "  lwz     r9,0(r3)"
_INSTR_WITH_OFFSET_RE = re.compile(
    r"^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(\S+)\s*(.*)?$"
)
_INSTR_NO_OFFSET_RE = re.compile(
    r"^\s+(\w[\w.]*)\s+(.*)?$"
)

# Known PPC opcodes that start an instruction (disambiguate from labels/directives)
_PPC_OPCODES = frozenset({
    "add", "addi", "addic", "addis", "addze", "and", "andi.", "andc",
    "b", "ba", "bc", "bcctr", "bclr", "bdnz", "beq", "bge", "bgt", "bl",
    "ble", "blt", "bne", "cmpw", "cmpwi", "cmplw", "cmplwi", "cntlzw",
    "clrlslwi", "clrlwi", "clrrwi", "crclr", "crandc", "creqv", "cror",
    "divw", "divwu", "eqv", "extsb", "extsh", "extrwi",
    "extrwi.", "fabs", "fadd", "fadds", "fcmpu", "fdiv", "fdivs",
    "fmadds", "fmsubs", "fmr", "fmul", "fmuls", "fneg", "fnmsubs",
    "frsp", "fsub", "fsubs",
    "lbz", "lbzx", "lfd", "lfs", "lfsx", "lha", "lhz", "li", "lis",
    "lwz", "lwzx", "lwzu",
    "mflr", "mr", "mtctr", "mtlr", "mulli", "mullw", "nand", "neg",
    "nor", "not", "or", "ori", "oris",
    "rlwimi", "rlwinm", "rlwinm.", "rotlwi", "slwi", "srawi", "srwi",
    "stb", "stbx", "std", "stfd", "stfs", "stfsx", "sth", "sthx",
    "stmw", "stw", "stwu", "stwx",
    "subf", "subf.", "subfc", "subfic", "subi",
    "xor", "xori", "xoris",
})

# Prologue helpers
_SAVEGPRLR_RE = re.compile(r"__savegprlr_(\d+)")
_SAVEFPR_RE = re.compile(r"__savefpr_(\d+)")

# Lines to skip
_SKIP_LINE_RE = re.compile(
    r"^\s*(\.|\$|TITLE|COMDAT|;|INCLUDE|PUBLIC|EXTRN|END\b|_TEXT|\.PPC|\.MODEL)"
)

# Label line
_LABEL_RE = re.compile(r"^\$\w+:")


def parse_asm_listing(
    text: str,
    function_name: str | None = None,
) -> AsmListing | None:
    """Parse a /FAs assembly listing into structured form.

    If function_name is provided, extracts only that function.
    Otherwise parses the first PROC..ENDP block found.

    Returns None if no matching function is found.
    """
    lines = text.splitlines()

    # Find function boundaries
    func_lines = _extract_function_lines(lines, function_name)
    if func_lines is None:
        return None

    func_name = function_name or "unknown"
    entries: list[AsmEntry] = []
    current_file = ""
    current_line = 0
    current_text = ""
    current_instrs: list[AsmInstruction] = []
    prologue_helper = None
    callee_saved = 0

    for line in func_lines:
        # File reference
        file_match = _FILE_REF_RE.match(line)
        if file_match:
            # Flush current entry
            if current_instrs:
                entries.append(AsmEntry(
                    source_file=current_file,
                    source_line=current_line,
                    source_text=current_text,
                    instructions=tuple(current_instrs),
                ))
                current_instrs = []
            current_file = _normalize_path(file_match.group(1))
            continue

        # Source line comment
        src_match = _SOURCE_LINE_RE.match(line)
        if src_match:
            # Flush current entry
            if current_instrs:
                entries.append(AsmEntry(
                    source_file=current_file,
                    source_line=current_line,
                    source_text=current_text,
                    instructions=tuple(current_instrs),
                ))
                current_instrs = []
            current_line = int(src_match.group(1))
            current_text = src_match.group(2).strip()
            continue

        # Skip labels, directives, etc.
        stripped = line.strip()
        if not stripped or _SKIP_LINE_RE.match(line) or _LABEL_RE.match(stripped):
            continue
        if stripped.startswith(".") or stripped.endswith(":"):
            continue

        # Try to parse as instruction
        instr = _parse_instruction_line(line)
        if instr is not None:
            current_instrs.append(instr)

            # Detect prologue helper
            if instr.opcode == "bl":
                for op in instr.operands:
                    m = _SAVEGPRLR_RE.search(op)
                    if m:
                        prologue_helper = op.strip()
                        callee_saved = 32 - int(m.group(1))
                    m = _SAVEFPR_RE.search(op)
                    if m and not prologue_helper:
                        prologue_helper = op.strip()

    # Flush final entry
    if current_instrs:
        entries.append(AsmEntry(
            source_file=current_file,
            source_line=current_line,
            source_text=current_text,
            instructions=tuple(current_instrs),
        ))

    return AsmListing(
        function_name=func_name,
        entries=entries,
        prologue_helper=prologue_helper,
        callee_saved_count=callee_saved,
    )


def _extract_function_lines(
    lines: list[str],
    function_name: str | None,
) -> list[str] | None:
    """Extract lines between PROC NEAR and ENDP for a function."""
    in_func = False
    result: list[str] = []

    for line in lines:
        if not in_func:
            m = _PROC_RE.match(line)
            if m:
                mangled = m.group(1)
                if function_name is None or function_name in mangled:
                    in_func = True
                    continue
        else:
            m = _ENDP_RE.match(line)
            if m:
                return result
            result.append(line)

    return result if in_func and result else None


def _parse_instruction_line(line: str) -> AsmInstruction | None:
    """Parse a single instruction line from a /FAs listing."""
    stripped = line.strip()
    if not stripped:
        return None

    # Try format with hex offset: "00008  81230000   lwz     r9,0(r3)"
    m = _INSTR_WITH_OFFSET_RE.match(line)
    if m:
        offset = int(m.group(1), 16)
        opcode = m.group(2).strip()
        operand_str = (m.group(3) or "").strip()
        operands = _split_operands(operand_str) if operand_str else []
        if _is_valid_opcode(opcode):
            return AsmInstruction(
                offset=offset,
                opcode=opcode,
                operands=operands,
                raw_text=stripped,
            )

    # Try format without offset: "  lwz     r9,0(r3)"
    m = _INSTR_NO_OFFSET_RE.match(line)
    if m:
        opcode = m.group(1).strip()
        operand_str = (m.group(2) or "").strip()
        operands = _split_operands(operand_str) if operand_str else []
        if _is_valid_opcode(opcode):
            return AsmInstruction(
                offset=None,
                opcode=opcode,
                operands=operands,
                raw_text=stripped,
            )

    return None


def _is_valid_opcode(s: str) -> bool:
    """Check if a string is a valid PPC opcode."""
    # Exact match or known prefix (for conditional branches like beq+0x10)
    return s.lower().rstrip(".") in _PPC_OPCODES or s.lower() in _PPC_OPCODES


def _split_operands(s: str) -> list[str]:
    """Split operand string into components."""
    if not s:
        return []
    # Strip trailing comments
    if ";" in s:
        s = s[:s.index(";")].strip()
    return [op.strip() for op in s.split(",") if op.strip()]


def _normalize_path(path: str) -> str:
    """Normalize a Windows-style path to Unix-style relative path."""
    path = path.replace("\\", "/")
    # Strip drive letter and absolute prefix
    if len(path) > 2 and path[1] == ":":
        path = path[2:]
    # Try to make relative by finding src/ or include/
    for marker in ("src/", "include/"):
        idx = path.find(marker)
        if idx >= 0:
            return path[idx:]
    return path.lstrip("/")


# ---------------------------------------------------------------------------
# Mismatch Attribution
# ---------------------------------------------------------------------------

def attribute_mismatches(
    listing: AsmListing,
    diff_instructions: list[dict],
) -> list[AttributedMismatch]:
    """Join objdiff instruction-level diffs with /FAs source attribution.

    diff_instructions should be a list of dicts with keys:
        - index: int (instruction index)
        - diff_kind: str ("match", "replace", "insert", "delete")
        - target_opcode: str | None
        - base_opcode: str | None
        - target_args: str | None (optional)
        - base_args: str | None (optional)

    Returns only non-matching instructions, attributed to their source lines.
    """
    attributed: list[AttributedMismatch] = []

    for diff in diff_instructions:
        kind = diff.get("diff_kind", "match")
        if kind == "match":
            continue

        index = diff.get("index", -1)
        target_op = diff.get("target_opcode", "")
        base_op = diff.get("base_opcode", "")

        # Classify mismatch type
        if kind == "insert":
            mtype = "extra"
        elif kind == "delete":
            mtype = "missing"
        elif target_op != base_op:
            mtype = "opcode"
        else:
            mtype = "register"

        # Look up source attribution from our listing
        entry = listing.source_line_for_index(index)
        if entry is not None and entry.source_line > 0:
            attributed.append(AttributedMismatch(
                instruction_index=index,
                target_opcode=target_op or "",
                base_opcode=base_op or "",
                mismatch_type=mtype,
                source_file=entry.source_file,
                source_line=entry.source_line,
                source_text=entry.source_text,
                confidence=0.9 if entry.source_line > 0 else 0.3,
            ))
        else:
            # Can't attribute — use interpolation from neighbors
            neighbor = _interpolate_source(listing, index)
            attributed.append(AttributedMismatch(
                instruction_index=index,
                target_opcode=target_op or "",
                base_opcode=base_op or "",
                mismatch_type=mtype,
                source_file=neighbor[0] if neighbor else None,
                source_line=neighbor[1] if neighbor else None,
                source_text=neighbor[2] if neighbor else None,
                confidence=0.4 if neighbor else 0.0,
            ))

    return attributed


def _interpolate_source(
    listing: AsmListing,
    index: int,
) -> tuple[str, int, str] | None:
    """Interpolate source location from neighboring instructions."""
    all_instrs = listing.all_instructions()
    total = len(all_instrs)

    # Search outward from index
    for delta in range(1, min(5, total)):
        for direction in (-delta, delta):
            neighbor_idx = index + direction
            if 0 <= neighbor_idx < total:
                entry, _ = all_instrs[neighbor_idx]
                if entry.source_line > 0:
                    return (entry.source_file, entry.source_line, entry.source_text)
    return None


# ---------------------------------------------------------------------------
# Region Aggregation
# ---------------------------------------------------------------------------

def aggregate_regions(
    mismatches: list[AttributedMismatch],
    listing: AsmListing,
    gap_tolerance: int = 2,
) -> list[MismatchRegion]:
    """Aggregate attributed mismatches into contiguous source regions.

    Mismatches on adjacent source lines (within gap_tolerance) are merged
    into a single region.
    """
    if not mismatches:
        return []

    # Group by source file
    by_file: dict[str, list[AttributedMismatch]] = {}
    unattributed: list[AttributedMismatch] = []
    for m in mismatches:
        if m.source_line is not None:
            # Use source_file or "<main>" for the compilation unit
            key = m.source_file or "<main>"
            by_file.setdefault(key, []).append(m)
        else:
            unattributed.append(m)

    regions: list[MismatchRegion] = []

    for src_file, file_mismatches in by_file.items():
        # Sort by source line
        sorted_mm = sorted(file_mismatches, key=lambda m: (m.source_line or 0))

        # Merge into contiguous regions
        current_start = sorted_mm[0].source_line or 0
        current_end = current_start
        current_group: list[AttributedMismatch] = [sorted_mm[0]]

        for m in sorted_mm[1:]:
            line = m.source_line or 0
            if line <= current_end + gap_tolerance:
                current_end = max(current_end, line)
                current_group.append(m)
            else:
                regions.append(_build_region(
                    src_file, current_start, current_end,
                    current_group, listing,
                ))
                current_start = line
                current_end = line
                current_group = [m]

        regions.append(_build_region(
            src_file, current_start, current_end,
            current_group, listing,
        ))

    # Add an unattributed region if any
    if unattributed:
        regions.append(MismatchRegion(
            source_file="<unknown>",
            start_line=0,
            end_line=0,
            source_lines=[],
            mismatches=unattributed,
            dominant_type=_dominant_type(unattributed),
            total_instructions=0,
            matched_instructions=0,
            unattributed_count=len(unattributed),
        ))

    # Sort by impact (most mismatches first)
    regions.sort(key=lambda r: -r.impact)
    return regions


def _build_region(
    source_file: str,
    start_line: int,
    end_line: int,
    mismatches: list[AttributedMismatch],
    listing: AsmListing,
) -> MismatchRegion:
    """Build a MismatchRegion from grouped mismatches."""
    # Count total instructions in this source line range
    total = 0
    source_texts: list[str] = []
    seen_lines: set[int] = set()
    for entry in listing.entries:
        entry_file = entry.source_file or "<main>"
        if entry_file == source_file and start_line <= entry.source_line <= end_line:
            total += len(entry.instructions)
            if entry.source_line not in seen_lines:
                source_texts.append(entry.source_text)
                seen_lines.add(entry.source_line)

    matched = total - len(mismatches)
    return MismatchRegion(
        source_file=source_file,
        start_line=start_line,
        end_line=end_line,
        source_lines=source_texts,
        mismatches=mismatches,
        dominant_type=_dominant_type(mismatches),
        total_instructions=total,
        matched_instructions=max(0, matched),
    )


def _dominant_type(mismatches: list[AttributedMismatch]) -> str:
    """Find the most common mismatch type."""
    if not mismatches:
        return "unknown"
    counts: dict[str, int] = {}
    for m in mismatches:
        counts[m.mismatch_type] = counts.get(m.mismatch_type, 0) + 1
    return max(counts, key=counts.get)  # type: ignore


# ---------------------------------------------------------------------------
# Convenience: full pipeline
# ---------------------------------------------------------------------------

def attribute_function(
    asm_text: str,
    function_name: str,
    diff_instructions: list[dict],
) -> tuple[AsmListing | None, list[AttributedMismatch], list[MismatchRegion]]:
    """Run the full attribution pipeline for a single function.

    Returns (listing, attributed_mismatches, mismatch_regions).
    """
    listing = parse_asm_listing(asm_text, function_name)
    if listing is None:
        return None, [], []

    attributed = attribute_mismatches(listing, diff_instructions)
    regions = aggregate_regions(attributed, listing)
    return listing, attributed, regions
