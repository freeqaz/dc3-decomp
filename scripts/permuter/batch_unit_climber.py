"""Batch unit climber — optimizes multiple functions in a single .cpp file simultaneously.

Uses O(1) compilation passes by combining variants from multiple functions
into a single source file, compiling it once, and using `objdiff --batch`
to get scores for all functions independently.
"""

from __future__ import annotations

import argparse
import difflib
import json
import sqlite3
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Any

from scripts.permuter.types import Variant, ScoreResult, extract_qualified_name
from scripts.permuter.generator import generate_variants
from scripts.permuter.patterns import get_all_patterns
from scripts.permuter.scorer import md5_file
from scripts.permuter.extractor import extract_function

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DB_PATH = REPO_ROOT / "decomp.db"
OBJDIFF_CLI = REPO_ROOT / "bin" / "objdiff-cli"

@dataclass
class _Edit:
    start: int
    end: int
    replacement: bytes

def extract_single_edit(original: bytes, modified: bytes, start_byte: int, end_byte: int) -> Optional[_Edit]:
    """Extract the exact byte change assuming only one function was modified."""
    if original == modified:
        return None
        
    len_diff = len(modified) - len(original)
    mod_end_byte = end_byte + len_diff
    
    if original[:start_byte] != modified[:start_byte] or original[end_byte:] != modified[mod_end_byte:]:
        # Fallback to difflib if our assumption is violated (e.g. includes were modified)
        matcher = difflib.SequenceMatcher(None, original, modified)
        edits = []
        for tag, i1, i2, j1, j2 in matcher.get_opcodes():
            if tag != "equal":
                edits.append(_Edit(i1, i2, modified[j1:j2]))
        if len(edits) == 1:
            return edits[0]
        # Return a single edit covering the whole file if multiple disjoint edits found
        return _Edit(0, len(original), modified)
        
    orig_body = original[start_byte:end_byte]
    mod_body = modified[start_byte:mod_end_byte]
    
    prefix_len = 0
    for i in range(min(len(orig_body), len(mod_body))):
        if orig_body[i] == mod_body[i]:
            prefix_len += 1
        else:
            break
            
    suffix_len = 0
    for i in range(min(len(orig_body) - prefix_len, len(mod_body) - prefix_len)):
        if orig_body[-(i+1)] == mod_body[-(i+1)]:
            suffix_len += 1
        else:
            break
            
    return _Edit(
        start=start_byte + prefix_len,
        end=end_byte - suffix_len,
        replacement=mod_body[prefix_len : len(mod_body) - suffix_len if suffix_len > 0 else None]
    )

def apply_edits(source: bytes, edits: list[_Edit]) -> bytes:
    edits = sorted(edits, key=lambda e: e.start, reverse=True)
    result = source
    for e in edits:
        result = result[:e.start] + e.replacement + result[e.end:]
    return result

@dataclass
class TargetFunction:
    db_id: int
    symbol: str
    qualified_name: str
    start_byte: int
    end_byte: int
    baseline_pct: float
    variants: list[Variant] = None
    best_variant: Optional[Variant] = None
    best_pct: float = 0.0
    
class UnitClimber:
    def __init__(self, source_path: Path):
        self.source_path = source_path
        self.original_source = source_path.read_bytes()
        self.functions: list[TargetFunction] = []
        self._compile_cmd: Optional[str] = None
        self._compile_cwd: Optional[str] = None
        self._obj_target = f"build/373307D9/{source_path.with_suffix('.obj')}"
        self._obj_path = Path(self._obj_target)
        
    def _extract_compile_cmd(self):
        import re
        result = subprocess.run(["ninja", "-t", "commands", self._obj_target], capture_output=True, text=True)
        for line in result.stdout.strip().splitlines():
            if line.startswith("cd "):
                parts = line.split(" && ", 1)
                self._compile_cwd = parts[0][3:]
                self._compile_cmd = parts[1]
                
                fo_match = re.search(r'/Fo(\S+)', self._compile_cmd)
                self._compile_fo_path = fo_match.group(1) if fo_match else None
                return
                
    def build_to_path(self, source: bytes, obj_out: Path) -> tuple[bool, str]:
        if not self._compile_cmd:
            self._extract_compile_cmd()
        
        # We must overwrite the real file for cl.exe to read it, but we can output to obj_out
        self.source_path.write_bytes(source)
        if self._compile_fo_path:
            cmd = self._compile_cmd.replace(f"/Fo{self._compile_fo_path}", f"/Fo{obj_out}")
        else:
            cmd = self._compile_cmd.replace(str(self._obj_path), str(obj_out))
        
        result = subprocess.run(cmd, shell=True, cwd=self._compile_cwd, capture_output=True, text=True)
        if result.returncode != 0:
            return False, result.stdout + result.stderr
        return True, ""
        
    def run_objdiff_batch(self, obj_path: Path, symbols: list[str]) -> dict[str, dict]:
        # Temporarily swap obj
        import threading
        import shutil
        tid = threading.get_ident()
        
        if self._obj_path.exists():
            backup_obj = self._obj_path.with_suffix(f".bak_{tid}.obj")
            shutil.copy2(self._obj_path, backup_obj)
        else:
            backup_obj = None
            
        shutil.copy2(obj_path, self._obj_path)
        
        stdin_data = "\\n".join(symbols) + "\\n"
        proc = subprocess.run(
            [str(OBJDIFF_CLI), "diff", "-p", str(REPO_ROOT), "-c", "functionRelocDiffs=none", "--batch"],
            input=stdin_data, capture_output=True, text=True
        )
        
        if backup_obj:
            shutil.copy2(backup_obj, self._obj_path)
            backup_obj.unlink(missing_ok=True)
        else:
            self._obj_path.unlink(missing_ok=True)
            
        results = {}
        if proc.stdout:
            for line in proc.stdout.strip().splitlines():
                if not line.startswith("{"): continue
                try:
                    data = json.loads(line)
                    sym = data.get("symbol")
                    if sym:
                        results[sym] = data
                except:
                    pass
        return results

    def load_functions(self, include_at_limit=False):
        conn = sqlite3.connect(str(DB_PATH))
        conn.row_factory = sqlite3.Row
        
        # Find unit path
        src_str = str(self.source_path)
        if src_str.startswith("src/"):
            unit = src_str[4:-4] # strip src/ and .cpp
        else:
            unit = self.source_path.stem
            
        query = "SELECT id, symbol, demangled, current_percent, verdict FROM functions WHERE unit LIKE '%' || ? || '%'"
        rows = conn.execute(query, [unit]).fetchall()
        conn.close()
        
        for row in rows:
            if not include_at_limit and row["verdict"] in ("COMPLETE", "AT_LIMIT"):
                continue
            if row["symbol"].startswith("merged_"):
                continue
                
            qual = extract_qualified_name(row["demangled"]) or row["symbol"]
            try:
                ctx = extract_function(self.source_path, qual)
                self.functions.append(TargetFunction(
                    db_id=row["id"],
                    symbol=row["symbol"],
                    qualified_name=qual,
                    start_byte=ctx.func_byte_range[0],
                    end_byte=ctx.func_byte_range[1],
                    baseline_pct=row["current_percent"],
                    best_pct=row["current_percent"],
                    variants=[]
                ))
            except Exception as e:
                msg = str(e).splitlines()[0] if str(e) else "Unknown error"
                print(f"Skipping {qual}: {msg}", file=sys.stderr)
                
    def climb(self, max_variants=50):
        if not self.functions:
            print("No workable functions found.")
            return
            
        print(f"Loaded {len(self.functions)} workable functions.")
        
        # Generate variants for all functions
        patterns = get_all_patterns()
        import tempfile
        tmp_dir = Path(tempfile.mkdtemp(prefix="unit_climber_"))
        final_edits = []
        
        try:
            # Generate Phase
            for f in self.functions:
                print(f"Generating for {f.qualified_name}...")
                try:
                    ctx = extract_function(self.source_path, f.qualified_name)
                    # We don't have guided diagnosis here easily, so we run unguided
                    variants_iter = generate_variants(ctx, patterns, max_variants=max_variants)
                    f.variants = list(variants_iter)
                    print(f"  -> {len(f.variants)} variants")
                except Exception as e:
                    print(f"  -> Failed: {e}")
                    f.variants = []
                    
            max_v = max((len(f.variants) for f in self.functions), default=0)
            if max_v == 0:
                print("No variants generated.")
                return
                
            import threading
            lock = threading.Lock()
            
            def _eval_batch(i: int):
                # Build super-variant
                edits = []
                for f in self.functions:
                    if i < len(f.variants):
                        e = extract_single_edit(self.original_source, f.variants[i].source, f.start_byte, f.end_byte)
                        if e: edits.append(e)
                
                if not edits: return
                
                new_source = apply_edits(self.original_source, edits)
                obj_out = tmp_dir / f"batch_{i}.obj"
                
                with lock:
                    ok, err = self.build_to_path(new_source, obj_out)
                
                    if not ok:
                        # Ignore build failure for now (one variant ruined it)
                        return
                        
                    symbols = [f.symbol for f in self.functions if i < len(f.variants)]
                    scores = self.run_objdiff_batch(obj_out, symbols)
                
                # Update bests
                for f in self.functions:
                    if i < len(f.variants) and f.symbol in scores:
                        pct = scores[f.symbol].get("fuzzy_match_percent", 0.0)
                        if pct > f.best_pct:
                            f.best_pct = pct
                            f.best_variant = f.variants[i]
                            print(f"[{f.qualified_name}] Improved to {pct:.2f}% with {f.variants[i].name}")

            # Evaluate Phase (parallelized compilation!)
            print(f"Evaluating {max_v} cross-function combinations...")
            with ThreadPoolExecutor(max_workers=6) as pool:
                futures = [pool.submit(_eval_batch, i) for i in range(max_v)]
                for future in as_completed(futures):
                    future.result() # raise exceptions if any
                    
            # Apply best variants
            final_edits = []
            for f in self.functions:
                if f.best_variant:
                    e = extract_single_edit(self.original_source, f.best_variant.source, f.start_byte, f.end_byte)
                    if e: final_edits.append(e)
                    
            if final_edits:
                print(f"Applying {len(final_edits)} winning variants.")
                final_source = apply_edits(self.original_source, final_edits)
                self.source_path.write_bytes(final_source)
            else:
                self.source_path.write_bytes(self.original_source) # restore just in case
                print("No improvements found.")
                
        finally:
            import shutil
            shutil.rmtree(tmp_dir, ignore_errors=True)
            # Restore original source just in case we crashed
            if self.source_path.read_bytes() != self.original_source and not final_edits:
                 self.source_path.write_bytes(self.original_source)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source_paths", nargs="*", type=Path, help="One or more .cpp files to optimize")
    parser.add_argument("--all", action="store_true", help="Find all files with workable functions and run on them")
    args = parser.parse_args()
    
    paths_to_run = []
    if args.all:
        import sqlite3
        conn = sqlite3.connect(str(DB_PATH))
        rows = conn.execute("SELECT DISTINCT unit FROM functions WHERE verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT')").fetchall()
        conn.close()
        for row in rows:
            u = row[0]
            if u.startswith("default/"):
                u = u[8:]
            p = REPO_ROOT / f"src/{u}.cpp"
            if p.exists():
                paths_to_run.append(p)
        print(f"Found {len(paths_to_run)} units with workable functions.")
    else:
        paths_to_run = args.source_paths

    if not paths_to_run:
        print("No files provided. Pass paths or --all")
        return

    for path in paths_to_run:
        print(f"\\n--- Optimizing {path} ---")
        climber = UnitClimber(path)
        climber.load_functions()
        climber.climb()

if __name__ == "__main__":
    main()
