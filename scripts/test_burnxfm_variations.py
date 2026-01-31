#!/usr/bin/env python3
"""
Test register allocation variations for RndMesh::BurnXfm
Based on advice for nudging MSVC register allocator for r28/r29 swaps.

Usage: python scripts/test_burnxfm_variations.py
"""
import subprocess
import sys
import json
from pathlib import Path

MESH_CPP = Path("src/system/rndobj/Mesh.cpp")
SYMBOL = "?BurnXfm@RndMesh@@QAAXXZ"

# Line range for BurnXfm function (821-834 inclusive, 0-indexed: 820-833)
FUNC_START = 820  # 0-indexed line for "void RndMesh::BurnXfm() {"
FUNC_END = 833    # 0-indexed line for closing "}"

# Variations to test (based on the document advice)
VARIATIONS = {
    "ORIGINAL": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 1. Make `end` an explicit local - end declared first
    "end_first": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>::iterator end = mChildren.end();",
        "        std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "        for (; it != end; ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 2. Iterator first, then end
    "it_first": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "        std::list<RndTransformable *>::iterator end = mChildren.end();",
        "        for (; it != end; ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 3. Alias for mLocalXfm
    "alias_localxfm": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        const Transform& localXfm = mLocalXfm;",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), localXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 4. Alias for mChildren
    "alias_children": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>& children = mChildren;",
        "        for (std::list<RndTransformable *>::iterator it = children.begin();",
        "             it != children.end();",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 5. Child pointer inside loop
    "child_inside": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            RndTransformable* child = *it;",
        "            Transform xfm;",
        "            Multiply(child->LocalXfm(), mLocalXfm, xfm);",
        "            child->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 6. Both aliases
    "both_aliases": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>& children = mChildren;",
        "        const Transform& localXfm = mLocalXfm;",
        "        for (std::list<RndTransformable *>::iterator it = children.begin();",
        "             it != children.end();",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), localXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 7. Self pointer alias (reg pressure shim)
    "self_alias": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        RndMesh* self = this;",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(self, false);",
        "    }",
        "}",
    ],

    # 8. xfm outside loop
    "xfm_outside": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        Transform xfm;",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 9. end + localXfm alias (forces both addresses earlier)
    "localxfm_end": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        const Transform& localXfm = mLocalXfm;",
        "        std::list<RndTransformable *>::iterator end = mChildren.end();",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != end;",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), localXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 10. end first, then localXfm alias (opposite order)
    "end_localxfm": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>::iterator end = mChildren.end();",
        "        const Transform& localXfm = mLocalXfm;",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != end;",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), localXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 11. end + child inside
    "end_child": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>::iterator end = mChildren.end();",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != end;",
        "             ++it) {",
        "            RndTransformable* child = *it;",
        "            Transform xfm;",
        "            Multiply(child->LocalXfm(), mLocalXfm, xfm);",
        "            child->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 12. xfm + end outside
    "xfm_end_outside": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        Transform xfm;",
        "        std::list<RndTransformable *>::iterator end = mChildren.end();",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != end;",
        "             ++it) {",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 13. Pointer to this stored, then use for final call
    "this_first": [
        "void RndMesh::BurnXfm() {",
        "    RndMesh* mesh = this;",
        "    if (mGeomOwner != mesh) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(mesh, false);",
        "    }",
        "}",
    ],

    # 14. Use while loop instead of for
    "while_loop": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "        while (it != mChildren.end()) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "            ++it;",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 15. Declare xfm before loop, child inside
    "xfm_before_child_in": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        Transform xfm;",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            RndTransformable* child = *it;",
        "            Multiply(child->LocalXfm(), mLocalXfm, xfm);",
        "            child->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 16. Use range-based style with explicit begin/end but stored
    "stored_begin_end": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>::iterator begin = mChildren.begin();",
        "        std::list<RndTransformable *>::iterator end = mChildren.end();",
        "        for (std::list<RndTransformable *>::iterator it = begin; it != end; ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 17. Store mLocalXfm pointer instead of ref
    "localxfm_ptr": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        const Transform* localXfm = &mLocalXfm;",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), *localXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 18. Dereference before Multiply call
    "deref_before_multiply": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            RndTransformable* t = *it;",
        "            Transform xfm;",
        "            const Transform& childXfm = t->LocalXfm();",
        "            Multiply(childXfm, mLocalXfm, xfm);",
        "            t->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 19. Alias children and use auto for iterator
    "children_auto": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>& c = mChildren;",
        "        for (std::list<RndTransformable *>::iterator it = c.begin();",
        "             it != c.end();",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 20. Move ::BurnXfm call inside else but before loop ends
    "burnxfm_inside": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm((RndTransformable*)this, false);",
        "    }",
        "}",
    ],

    # 21. Double dereference style
    "double_deref": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        std::list<RndTransformable *>::iterator it;",
        "        for (it = mChildren.begin(); it != mChildren.end(); ++it) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],

    # 22. Separated increment
    "sep_increment": [
        "void RndMesh::BurnXfm() {",
        "    if (mGeomOwner != this) {",
        '        MILO_NOTIFY("Must be geom owner to burn xfm");',
        "    } else {",
        "        for (std::list<RndTransformable *>::iterator it = mChildren.begin();",
        "             it != mChildren.end();",
        "             ) {",
        "            Transform xfm;",
        "            Multiply((*it)->LocalXfm(), mLocalXfm, xfm);",
        "            (*it)->SetLocalXfm(xfm);",
        "            ++it;",
        "        }",
        "        ::BurnXfm(this, false);",
        "    }",
        "}",
    ],
}

def apply_variation(lines, variation_lines):
    """Replace the function with the variation"""
    result = lines[:FUNC_START] + variation_lines + lines[FUNC_END + 1:]
    return result

def run_objdiff():
    """Run objdiff and return match percentage"""
    # Build first
    build = subprocess.run(["ninja"], capture_output=True, text=True)
    if build.returncode != 0:
        # Check stderr for actual error
        if "error" in build.stderr.lower() or "error" in build.stdout.lower():
            print(f"    Build error")
            return 0.0

    result = subprocess.run(
        ["./bin/objdiff-cli", "diff", "-p", ".", SYMBOL, "-f", "json"],
        capture_output=True, text=True
    )
    try:
        data = json.loads(result.stdout)
        return data.get("fuzzy_match_percent", 0)
    except Exception as e:
        print(f"    Parse error: {e}")
        return 0.0

def main():
    # Read original file
    original_lines = MESH_CPP.read_text().splitlines()

    results = []

    # Test each variation
    for name, var_lines in VARIATIONS.items():
        print(f"Testing {name}...", end=" ", flush=True)

        modified_lines = apply_variation(original_lines, var_lines)
        MESH_CPP.write_text("\n".join(modified_lines) + "\n")

        match = run_objdiff()
        results.append((name, match))
        print(f"{match:.2f}%")

    # Restore original
    MESH_CPP.write_text("\n".join(original_lines) + "\n")
    print("\nRestored original file.")

    # Sort by match percentage
    results.sort(key=lambda x: x[1], reverse=True)
    baseline = next(m for n, m in results if n == "ORIGINAL")

    print("\n" + "="*60)
    print("RESULTS (sorted by match %):")
    print("="*60)
    for name, match in results:
        marker = ""
        if match > baseline:
            marker = " <-- IMPROVED!"
        elif match == baseline and name != "ORIGINAL":
            marker = " (same)"
        elif match < baseline and match > 0:
            marker = " (worse)"
        print(f"  {name:25s}: {match:6.2f}%{marker}")

if __name__ == "__main__":
    main()
