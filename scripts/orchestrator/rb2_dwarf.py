"""RB2 DWARF dump parser for DC3 decomp.

Parses the rb2_dump.cpp file to extract class/struct information
including member offsets, sizes, and inheritance hierarchies.

This provides supplementary type information for decomp work,
especially useful when DC3 headers lack offset details.
"""

import json
import re
import sqlite3
from pathlib import Path
from typing import Any

# Default path to RB2 DWARF dump
DEFAULT_RB2_DUMP = Path.home() / "code/milohax/rb3/doc/rb2_dump.cpp"
DEFAULT_CACHE_PATH = Path("rb2_dwarf_cache.json")


class RB2DwarfParser:
    """Parser for RB2 DWARF dump file."""

    def __init__(self, dump_path: Path = DEFAULT_RB2_DUMP):
        self.dump_path = dump_path
        self._classes: dict[str, dict] = {}
        self._parsed = False

    def parse(self) -> dict[str, dict]:
        """
        Parse the RB2 dump file and extract class information.

        Returns:
            Dict mapping class name -> class info
        """
        if self._parsed:
            return self._classes

        if not self.dump_path.exists():
            raise FileNotFoundError(f"RB2 dump not found: {self.dump_path}")

        content = self.dump_path.read_text(errors="replace")
        self._parse_content(content)
        self._parsed = True

        return self._classes

    def _parse_content(self, content: str) -> None:
        """Parse the dump content."""
        # Regex patterns
        # Class/struct definition: class Name : public Parent { or class Name {
        class_pattern = re.compile(
            r'^(class|struct)\s+(\w+)(?:\s*:\s*(?:public|protected|private)?\s*(.+?))?\s*\{',
            re.MULTILINE
        )

        # Total size comment: // total size: 0xNN
        size_pattern = re.compile(r'//\s*total size:\s*(0x[0-9a-fA-F]+|\d+)')

        # Member with offset: type name; // offset 0xNN, size 0xNN
        member_pattern = re.compile(
            r'^\s*(.+?)\s+(\w+);\s*//\s*offset\s*(0x[0-9a-fA-F]+|\d+),\s*size\s*(0x[0-9a-fA-F]+|\d+)',
            re.MULTILINE
        )

        # Enum definition: enum Name { ... };
        enum_pattern = re.compile(r'^enum\s+(\w+)\s*\{([^}]+)\}', re.MULTILINE | re.DOTALL)

        # Find all class/struct definitions
        lines = content.split('\n')
        i = 0
        while i < len(lines):
            line = lines[i]

            # Check for class/struct start
            class_match = class_pattern.match(line)
            if class_match:
                kind = class_match.group(1)  # class or struct
                name = class_match.group(2)
                parents_str = class_match.group(3) or ""

                # Parse parent classes
                parents = []
                if parents_str:
                    for parent in parents_str.split(','):
                        parent = parent.strip()
                        # Remove access specifiers
                        for spec in ['public ', 'protected ', 'private ', 'virtual ']:
                            parent = parent.replace(spec, '')
                        parent = parent.strip()
                        if parent:
                            parents.append(parent)

                # Find the class body
                brace_count = 1
                body_lines = []
                i += 1

                while i < len(lines) and brace_count > 0:
                    body_line = lines[i]
                    brace_count += body_line.count('{') - body_line.count('}')
                    body_lines.append(body_line)
                    i += 1

                body = '\n'.join(body_lines)

                # Extract total size
                total_size = 0
                size_match = size_pattern.search(body)
                if size_match:
                    size_str = size_match.group(1)
                    total_size = int(size_str, 16) if size_str.startswith('0x') else int(size_str)

                # Extract members
                members = []
                for m in member_pattern.finditer(body):
                    type_str = m.group(1).strip()
                    member_name = m.group(2)
                    offset_str = m.group(3)
                    size_str = m.group(4)

                    offset = int(offset_str, 16) if offset_str.startswith('0x') else int(offset_str)
                    size = int(size_str, 16) if size_str.startswith('0x') else int(size_str)

                    members.append({
                        'name': member_name,
                        'type': type_str,
                        'offset': offset,
                        'size': size,
                    })

                # Sort members by offset
                members.sort(key=lambda x: x['offset'])

                # Store class info (keep first definition if duplicate)
                if name not in self._classes:
                    self._classes[name] = {
                        'kind': kind,
                        'name': name,
                        'parents': parents,
                        'total_size': total_size,
                        'members': members,
                    }

            else:
                i += 1

    def get_class(self, name: str) -> dict | None:
        """Get class info by name."""
        self.parse()
        return self._classes.get(name)

    def get_member_at_offset(self, class_name: str, offset: int) -> dict | None:
        """
        Find member at specific offset in a class.

        Searches the class and its parents.

        Args:
            class_name: Class name
            offset: Byte offset to look up

        Returns:
            Member dict or None
        """
        self.parse()

        class_info = self._classes.get(class_name)
        if not class_info:
            return None

        # Search this class's members
        for member in class_info['members']:
            if member['offset'] == offset:
                return {
                    'class': class_name,
                    'member': member['name'],
                    'type': member['type'],
                    'offset': offset,
                    'size': member['size'],
                }
            # Check if offset falls within this member
            if member['offset'] <= offset < member['offset'] + member['size']:
                return {
                    'class': class_name,
                    'member': member['name'],
                    'type': member['type'],
                    'offset': member['offset'],
                    'size': member['size'],
                    'sub_offset': offset - member['offset'],
                }

        # Search parent classes recursively
        for parent in class_info['parents']:
            result = self.get_member_at_offset(parent, offset)
            if result:
                return result

        return None

    def get_inheritance_chain(self, class_name: str) -> list[str]:
        """Get full inheritance chain for a class."""
        self.parse()

        chain = []
        visited = set()

        def visit(name: str):
            if name in visited:
                return
            visited.add(name)
            chain.append(name)

            class_info = self._classes.get(name)
            if class_info:
                for parent in class_info['parents']:
                    visit(parent)

        visit(class_name)
        return chain

    def search_classes(self, pattern: str) -> list[str]:
        """Search for classes matching a pattern."""
        self.parse()

        regex = re.compile(pattern, re.IGNORECASE)
        return [name for name in self._classes.keys() if regex.search(name)]

    def to_cache(self, cache_path: Path = DEFAULT_CACHE_PATH) -> None:
        """Save parsed data to JSON cache."""
        self.parse()
        with open(cache_path, 'w') as f:
            json.dump(self._classes, f, indent=2)

    @classmethod
    def from_cache(cls, cache_path: Path = DEFAULT_CACHE_PATH) -> "RB2DwarfParser":
        """Load parser from JSON cache."""
        parser = cls()
        if cache_path.exists():
            with open(cache_path) as f:
                parser._classes = json.load(f)
                parser._parsed = True
        return parser


class RB2DwarfDB:
    """SQLite-backed RB2 DWARF data for fast queries."""

    def __init__(self, db_path: str | Path = "rb2_dwarf.db"):
        self.db_path = str(db_path)
        self._conn: sqlite3.Connection | None = None

    def _get_conn(self) -> sqlite3.Connection:
        if self._conn is None:
            self._conn = sqlite3.connect(self.db_path)
            self._conn.row_factory = sqlite3.Row
        return self._conn

    def build_from_parser(self, parser: RB2DwarfParser) -> dict[str, int]:
        """Build database from parsed DWARF data."""
        parser.parse()

        conn = self._get_conn()
        conn.executescript("""
            DROP TABLE IF EXISTS rb2_classes;
            DROP TABLE IF EXISTS rb2_members;
            DROP TABLE IF EXISTS rb2_inheritance;

            CREATE TABLE rb2_classes (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL UNIQUE,
                kind TEXT,
                total_size INTEGER
            );

            CREATE TABLE rb2_members (
                id INTEGER PRIMARY KEY,
                class_id INTEGER REFERENCES rb2_classes(id),
                name TEXT NOT NULL,
                type TEXT,
                offset INTEGER,
                size INTEGER
            );

            CREATE TABLE rb2_inheritance (
                id INTEGER PRIMARY KEY,
                class_id INTEGER REFERENCES rb2_classes(id),
                parent_name TEXT NOT NULL
            );

            CREATE INDEX idx_rb2_members_offset ON rb2_members(class_id, offset);
            CREATE INDEX idx_rb2_classes_name ON rb2_classes(name);
        """)

        classes_inserted = 0
        members_inserted = 0

        for name, info in parser._classes.items():
            cursor = conn.execute(
                "INSERT INTO rb2_classes (name, kind, total_size) VALUES (?, ?, ?)",
                (name, info['kind'], info['total_size'])
            )
            class_id = cursor.lastrowid
            classes_inserted += 1

            for member in info['members']:
                conn.execute(
                    "INSERT INTO rb2_members (class_id, name, type, offset, size) VALUES (?, ?, ?, ?, ?)",
                    (class_id, member['name'], member['type'], member['offset'], member['size'])
                )
                members_inserted += 1

            for parent in info['parents']:
                conn.execute(
                    "INSERT INTO rb2_inheritance (class_id, parent_name) VALUES (?, ?)",
                    (class_id, parent)
                )

        conn.commit()
        return {'classes': classes_inserted, 'members': members_inserted}

    def get_class(self, name: str) -> dict | None:
        """Get class info by name."""
        conn = self._get_conn()

        row = conn.execute(
            "SELECT id, name, kind, total_size FROM rb2_classes WHERE name = ?",
            (name,)
        ).fetchone()

        if not row:
            return None

        class_id = row['id']

        # Get members
        members = conn.execute(
            "SELECT name, type, offset, size FROM rb2_members WHERE class_id = ? ORDER BY offset",
            (class_id,)
        ).fetchall()

        # Get parents
        parents = conn.execute(
            "SELECT parent_name FROM rb2_inheritance WHERE class_id = ?",
            (class_id,)
        ).fetchall()

        return {
            'name': row['name'],
            'kind': row['kind'],
            'total_size': row['total_size'],
            'members': [dict(m) for m in members],
            'parents': [p['parent_name'] for p in parents],
        }

    def lookup_offset(self, class_name: str, offset: int) -> dict | None:
        """Look up what member is at a specific offset."""
        conn = self._get_conn()

        # Get class ID
        class_row = conn.execute(
            "SELECT id FROM rb2_classes WHERE name = ?", (class_name,)
        ).fetchone()

        if not class_row:
            return None

        class_id = class_row['id']

        # Look for exact match first
        member = conn.execute(
            "SELECT name, type, offset, size FROM rb2_members WHERE class_id = ? AND offset = ?",
            (class_id, offset)
        ).fetchone()

        if member:
            return {
                'class': class_name,
                'member': member['name'],
                'type': member['type'],
                'offset': member['offset'],
                'size': member['size'],
            }

        # Look for member containing the offset
        member = conn.execute(
            "SELECT name, type, offset, size FROM rb2_members WHERE class_id = ? AND offset <= ? AND offset + size > ?",
            (class_id, offset, offset)
        ).fetchone()

        if member:
            return {
                'class': class_name,
                'member': member['name'],
                'type': member['type'],
                'offset': member['offset'],
                'size': member['size'],
                'sub_offset': offset - member['offset'],
            }

        # Search parents
        parents = conn.execute(
            "SELECT parent_name FROM rb2_inheritance WHERE class_id = ?",
            (class_id,)
        ).fetchall()

        for parent_row in parents:
            result = self.lookup_offset(parent_row['parent_name'], offset)
            if result:
                return result

        return None

    def search_classes(self, pattern: str) -> list[str]:
        """Search for classes matching pattern (SQL LIKE)."""
        conn = self._get_conn()
        rows = conn.execute(
            "SELECT name FROM rb2_classes WHERE name LIKE ? ORDER BY name",
            (f'%{pattern}%',)
        ).fetchall()
        return [r['name'] for r in rows]

    def get_stats(self) -> dict[str, int]:
        """Get database statistics."""
        conn = self._get_conn()
        classes = conn.execute("SELECT COUNT(*) FROM rb2_classes").fetchone()[0]
        members = conn.execute("SELECT COUNT(*) FROM rb2_members").fetchone()[0]
        return {'classes': classes, 'members': members}

    def close(self):
        """Close database connection."""
        if self._conn:
            self._conn.close()
            self._conn = None


# Module-level convenience functions
_parser: RB2DwarfParser | None = None
_db: RB2DwarfDB | None = None


def get_parser(dump_path: Path = DEFAULT_RB2_DUMP) -> RB2DwarfParser:
    """Get or create the global parser instance."""
    global _parser
    if _parser is None:
        _parser = RB2DwarfParser(dump_path)
    return _parser


def get_db(db_path: str | Path = "rb2_dwarf.db") -> RB2DwarfDB:
    """Get or create the global database instance."""
    global _db
    if _db is None:
        _db = RB2DwarfDB(db_path)
    return _db


def lookup_rb2_class(name: str) -> dict | None:
    """Convenience function to look up a class."""
    return get_parser().get_class(name)


def lookup_rb2_offset(class_name: str, offset: int) -> dict | None:
    """Convenience function to look up an offset."""
    return get_parser().get_member_at_offset(class_name, offset)
