#!/usr/bin/env python3
"""
Struct offset resolution tool for DC3 decomp.

Parses annotated headers to build a struct offset lookup database.
When objdiff reports offset mismatches, agents can query which struct field
is being accessed.

Usage:
    ./tools/struct_db.py build [paths...] [--db struct_db.sqlite] [-v]
    ./tools/struct_db.py lookup <class> <offset> [--db struct_db.sqlite]
    ./tools/struct_db.py info <class> [--db struct_db.sqlite]
    ./tools/struct_db.py list [--pattern PAT] [--db struct_db.sqlite]
"""

import argparse
import fnmatch
import re
import sqlite3
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple


@dataclass
class Member:
    """A class/struct member with offset annotation."""
    name: str
    type_str: str
    offset: int
    line_number: int


@dataclass
class ClassInfo:
    """Parsed class/struct information."""
    name: str
    file_path: str
    parents: List[str] = field(default_factory=list)
    is_virtual_inheritance: Dict[str, bool] = field(default_factory=dict)
    members: List[Member] = field(default_factory=list)
    is_struct: bool = False


# Regex patterns
# Class/struct declaration with optional inheritance
CLASS_DECL_RE = re.compile(
    r'^\s*(?:class|struct)\s+(\w+)(?:\s*:\s*(.+?))?\s*\{',
    re.MULTILINE
)

# Member with offset annotation
# Handles: ObjPtr<T>, std::vector<T>, Type*, Type&, arrays, etc.
# Pattern captures: (type) (*|&)? (name) [array]? ; // 0xHEX
MEMBER_RE = re.compile(
    r'^\s*([^;]+?)\s*([*&]?)\s*(\w+)(?:\s*\[[^\]]*\])?\s*;\s*//\s*0x([0-9a-fA-F]+)',
    re.MULTILINE
)

# Parse inheritance list like "public Foo, virtual public Bar, private Baz"
INHERIT_RE = re.compile(
    r'(virtual\s+)?(public|private|protected)\s+([\w:]+)'
)


class StructDB:
    """SQLite-backed struct offset database."""

    def __init__(self, db_path: str = "struct_db.sqlite"):
        self.db_path = db_path
        self.conn: Optional[sqlite3.Connection] = None

    def connect(self):
        """Connect to the database."""
        self.conn = sqlite3.connect(self.db_path)
        self.conn.execute("PRAGMA journal_mode = WAL")
        self.conn.row_factory = sqlite3.Row

    def close(self):
        """Close the database connection."""
        if self.conn:
            self.conn.close()
            self.conn = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def create_schema(self):
        """Create database schema."""
        cursor = self.conn.cursor()
        cursor.executescript("""
            CREATE TABLE IF NOT EXISTS classes (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                file_path TEXT,
                is_struct INTEGER DEFAULT 0,
                UNIQUE(name, file_path)
            );

            CREATE TABLE IF NOT EXISTS inheritance (
                child_id INTEGER REFERENCES classes(id),
                parent_name TEXT NOT NULL,
                is_virtual INTEGER DEFAULT 0,
                order_idx INTEGER
            );

            CREATE TABLE IF NOT EXISTS members (
                class_id INTEGER REFERENCES classes(id),
                name TEXT NOT NULL,
                type_str TEXT,
                offset INTEGER NOT NULL
            );

            CREATE INDEX IF NOT EXISTS idx_members_class_offset
            ON members(class_id, offset);

            CREATE INDEX IF NOT EXISTS idx_classes_name
            ON classes(name);
        """)
        self.conn.commit()

    def clear(self):
        """Clear all data from the database."""
        cursor = self.conn.cursor()
        cursor.executescript("""
            DELETE FROM members;
            DELETE FROM inheritance;
            DELETE FROM classes;
        """)
        self.conn.commit()

    def insert_class(self, info: ClassInfo) -> int:
        """Insert a class and return its ID."""
        cursor = self.conn.cursor()

        # Insert or replace the class
        cursor.execute("""
            INSERT OR REPLACE INTO classes (name, file_path, is_struct)
            VALUES (?, ?, ?)
        """, (info.name, info.file_path, 1 if info.is_struct else 0))

        class_id = cursor.lastrowid

        # Delete existing inheritance and members for this class
        cursor.execute("DELETE FROM inheritance WHERE child_id = ?", (class_id,))
        cursor.execute("DELETE FROM members WHERE class_id = ?", (class_id,))

        # Insert inheritance
        for idx, parent in enumerate(info.parents):
            is_virtual = info.is_virtual_inheritance.get(parent, False)
            cursor.execute("""
                INSERT INTO inheritance (child_id, parent_name, is_virtual, order_idx)
                VALUES (?, ?, ?, ?)
            """, (class_id, parent, 1 if is_virtual else 0, idx))

        # Insert members
        for member in info.members:
            cursor.execute("""
                INSERT INTO members (class_id, name, type_str, offset)
                VALUES (?, ?, ?, ?)
            """, (class_id, member.name, member.type_str, member.offset))

        return class_id

    def get_class_id(self, class_name: str) -> Optional[int]:
        """Get class ID by name."""
        cursor = self.conn.cursor()
        cursor.execute(
            "SELECT id FROM classes WHERE name = ?",
            (class_name,)
        )
        row = cursor.fetchone()
        return row['id'] if row else None

    def get_class_info(self, class_name: str) -> Optional[Dict]:
        """Get full class info including parents and members."""
        cursor = self.conn.cursor()

        cursor.execute(
            "SELECT * FROM classes WHERE name = ?",
            (class_name,)
        )
        class_row = cursor.fetchone()
        if not class_row:
            return None

        class_id = class_row['id']

        # Get parents
        cursor.execute("""
            SELECT parent_name, is_virtual FROM inheritance
            WHERE child_id = ? ORDER BY order_idx
        """, (class_id,))
        parents = [(row['parent_name'], bool(row['is_virtual']))
                   for row in cursor.fetchall()]

        # Get members
        cursor.execute("""
            SELECT name, type_str, offset FROM members
            WHERE class_id = ? ORDER BY offset
        """, (class_id,))
        members = [dict(row) for row in cursor.fetchall()]

        return {
            'name': class_row['name'],
            'file_path': class_row['file_path'],
            'is_struct': bool(class_row['is_struct']),
            'parents': parents,
            'members': members
        }

    def resolve_inheritance_chain(self, class_name: str) -> List[str]:
        """Get ordered list of all parent classes (depth-first)."""
        cursor = self.conn.cursor()
        visited = set()
        chain = []

        def visit(name: str):
            if name in visited:
                return
            visited.add(name)

            # Get class ID
            cursor.execute("SELECT id FROM classes WHERE name = ?", (name,))
            row = cursor.fetchone()
            if not row:
                return

            class_id = row['id']

            # Get parents in order
            cursor.execute("""
                SELECT parent_name FROM inheritance
                WHERE child_id = ? ORDER BY order_idx
            """, (class_id,))

            for parent_row in cursor.fetchall():
                parent_name = parent_row['parent_name']
                visit(parent_name)
                if parent_name not in chain:
                    chain.append(parent_name)

        visit(class_name)
        return chain

    def lookup(self, class_name: str, offset: int) -> Optional[Tuple[str, str, str]]:
        """
        Look up field at offset, checking inheritance chain.
        Returns (class_name, member_name, type_str) or None.
        """
        cursor = self.conn.cursor()

        # Classes to check: the class itself plus all parents
        classes_to_check = [class_name] + self.resolve_inheritance_chain(class_name)

        for check_class in classes_to_check:
            cursor.execute("""
                SELECT c.name, m.name, m.type_str
                FROM members m
                JOIN classes c ON m.class_id = c.id
                WHERE c.name = ? AND m.offset = ?
            """, (check_class, offset))

            row = cursor.fetchone()
            if row:
                return (row[0], row[1], row[2])

        return None

    def list_classes(self, pattern: Optional[str] = None) -> List[Dict]:
        """List all classes, optionally filtered by pattern."""
        cursor = self.conn.cursor()
        cursor.execute("SELECT name, file_path, is_struct FROM classes ORDER BY name")

        results = []
        for row in cursor.fetchall():
            name = row['name']
            if pattern and not fnmatch.fnmatch(name, pattern):
                continue
            results.append({
                'name': name,
                'file_path': row['file_path'],
                'is_struct': bool(row['is_struct'])
            })

        return results

    def build_from_paths(self, paths: List[Path], verbose: bool = False):
        """Parse headers from paths and build database."""
        self.create_schema()
        self.clear()

        # Collect all header files
        header_files = []
        for path in paths:
            if path.is_file():
                header_files.append(path)
            elif path.is_dir():
                header_files.extend(path.rglob("*.h"))

        total_classes = 0
        total_members = 0

        for header_path in header_files:
            classes = parse_header(header_path)
            for cls in classes:
                self.insert_class(cls)
                total_classes += 1
                total_members += len(cls.members)
                if verbose:
                    print(f"  {cls.name}: {len(cls.members)} members")

        self.conn.commit()
        return total_classes, total_members


def parse_inheritance(inherit_str: str) -> Tuple[List[str], Dict[str, bool]]:
    """Parse inheritance string into parent list and virtual flags."""
    parents = []
    is_virtual = {}

    for match in INHERIT_RE.finditer(inherit_str):
        virtual = match.group(1) is not None
        parent_name = match.group(3)
        # Strip namespace prefix if present (e.g., Hmx::Object -> Object)
        # Keep full name for now since we might want it
        parents.append(parent_name)
        is_virtual[parent_name] = virtual

    return parents, is_virtual


def parse_header(path: Path) -> List[ClassInfo]:
    """Parse a header file and extract class/struct information."""
    try:
        content = path.read_text(encoding='utf-8', errors='ignore')
    except Exception:
        return []

    classes = []
    lines = content.split('\n')

    # Track nested class context
    class_stack = []
    current_class: Optional[ClassInfo] = None
    brace_depth = 0
    class_start_depth = 0

    i = 0
    while i < len(lines):
        line = lines[i]

        # Check for class/struct declaration
        # We need to be careful about forward declarations
        stripped = line.strip()

        class_match = re.match(
            r'^(class|struct)\s+(\w+)(?:\s*:\s*(.+?))?\s*\{',
            stripped
        )

        if not class_match:
            # Check if declaration spans multiple lines
            if re.match(r'^(class|struct)\s+(\w+)\s*:', stripped):
                # Inheritance on next line
                combined = stripped
                j = i + 1
                while j < len(lines) and '{' not in combined:
                    combined += ' ' + lines[j].strip()
                    j += 1
                class_match = re.match(
                    r'^(class|struct)\s+(\w+)\s*:\s*(.+?)\s*\{',
                    combined
                )

        if class_match:
            keyword = class_match.group(1)
            class_name = class_match.group(2)
            inherit_str = class_match.group(3) or ""

            # Handle nested classes
            if current_class:
                class_stack.append((current_class, class_start_depth))
                class_name = f"{current_class.name}::{class_name}"

            parents, is_virtual_dict = parse_inheritance(inherit_str)

            current_class = ClassInfo(
                name=class_name,
                file_path=str(path),
                parents=parents,
                is_virtual_inheritance=is_virtual_dict,
                is_struct=(keyword == 'struct')
            )
            class_start_depth = brace_depth

        # Track brace depth
        brace_depth += line.count('{') - line.count('}')

        # Check if we've exited the current class
        if current_class and brace_depth <= class_start_depth:
            classes.append(current_class)
            if class_stack:
                current_class, class_start_depth = class_stack.pop()
            else:
                current_class = None

        # Check for member with offset annotation
        if current_class:
            member_match = re.match(
                r'\s*([^;]+?)\s*([*&]?)\s*(\w+)(?:\s*\[[^\]]*\])?\s*;\s*//\s*0x([0-9a-fA-F]+)',
                line
            )
            if member_match:
                type_str = member_match.group(1).strip()
                ptr_ref = member_match.group(2)
                if ptr_ref:
                    type_str += ' ' + ptr_ref
                name = member_match.group(3)
                offset = int(member_match.group(4), 16)

                current_class.members.append(Member(
                    name=name,
                    type_str=type_str,
                    offset=offset,
                    line_number=i + 1
                ))

        i += 1

    # Don't forget the last class if file doesn't end cleanly
    if current_class:
        classes.append(current_class)

    return classes


def cmd_build(args):
    """Build database from headers."""
    paths = [Path(p) for p in args.paths] if args.paths else [Path("src/")]

    with StructDB(args.db) as db:
        total_classes, total_members = db.build_from_paths(paths, args.verbose)

    print(f"Built database: {total_classes} classes, {total_members} members")
    print(f"Saved to: {args.db}")


def cmd_lookup(args):
    """Look up field at offset."""
    # Parse offset (hex or decimal)
    if args.offset.startswith('0x'):
        offset = int(args.offset, 16)
    else:
        offset = int(args.offset)

    with StructDB(args.db) as db:
        result = db.lookup(args.class_name, offset)

    if result:
        cls_name, member_name, type_str = result
        print(f"{cls_name}::{member_name} ({type_str})")
    else:
        print(f"No field found at offset 0x{offset:x} in {args.class_name}")


def cmd_info(args):
    """Show class info."""
    with StructDB(args.db) as db:
        info = db.get_class_info(args.class_name)

    if not info:
        print(f"Class not found: {args.class_name}")
        return

    keyword = "struct" if info['is_struct'] else "class"
    print(f"{keyword} {info['name']}")
    print(f"  File: {info['file_path']}")

    if info['parents']:
        print("  Parents:")
        for parent, is_virtual in info['parents']:
            v = " (virtual)" if is_virtual else ""
            print(f"    - {parent}{v}")

    # Get full inheritance chain
    with StructDB(args.db) as db:
        chain = db.resolve_inheritance_chain(args.class_name)
    if chain:
        print(f"  Full inheritance chain: {' -> '.join(chain)}")

    if info['members']:
        print("  Members:")
        for m in info['members']:
            print(f"    0x{m['offset']:02x}: {m['type_str']} {m['name']}")


def cmd_list(args):
    """List classes."""
    with StructDB(args.db) as db:
        classes = db.list_classes(args.pattern)

    if not classes:
        print("No classes found")
        return

    for cls in classes:
        keyword = "struct" if cls['is_struct'] else "class"
        print(f"{keyword} {cls['name']}")


def main():
    parser = argparse.ArgumentParser(
        description="Struct offset database for DC3 decomp"
    )
    parser.add_argument(
        '--db', default='struct_db.sqlite',
        help='Database file path (default: struct_db.sqlite)'
    )

    subparsers = parser.add_subparsers(dest='command', required=True)

    # build command
    build_parser = subparsers.add_parser('build', help='Build database from headers')
    build_parser.add_argument(
        'paths', nargs='*',
        help='Paths to scan (default: src/)'
    )
    build_parser.add_argument(
        '-v', '--verbose', action='store_true',
        help='Verbose output'
    )

    # lookup command
    lookup_parser = subparsers.add_parser('lookup', help='Look up field at offset')
    lookup_parser.add_argument('class_name', help='Class name')
    lookup_parser.add_argument('offset', help='Offset (hex with 0x prefix or decimal)')

    # info command
    info_parser = subparsers.add_parser('info', help='Show class info')
    info_parser.add_argument('class_name', help='Class name')

    # list command
    list_parser = subparsers.add_parser('list', help='List classes')
    list_parser.add_argument(
        '--pattern', '-p',
        help='Filter by glob pattern (e.g., Rnd*)'
    )

    args = parser.parse_args()

    if args.command == 'build':
        cmd_build(args)
    elif args.command == 'lookup':
        cmd_lookup(args)
    elif args.command == 'info':
        cmd_info(args)
    elif args.command == 'list':
        cmd_list(args)


if __name__ == '__main__':
    main()
