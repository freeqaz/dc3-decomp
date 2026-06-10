"""Shared definition of SDK/vendor unit prefixes for DC3 decomp scripts.

These prefixes identify un-authorable third-party or SDK units that should be
excluded from the authorable-denominator progress metrics.  Every script that
needs this list should import it from here so the definition stays in one
place.

Background:
  - ``default/xdk/*``         — Xbox Dev Kit (D3D9/D3DX9, XAudio2, NUI/Kinect
                                 speech, xgraphics shader compiler, LIBCMT, …).
                                 No ``src/*.cpp`` exists for any XDK unit.
  - ``default/lib/binkxenon/`` — RAD Game Tools Bink video SDK.
                                 All 17 units have no ``src/*.cpp``.
  Together these two trees account for ≈44 % of the binary's total_code bytes
  (11.38 MB total → ~5.03 MB un-authorable).  They are intentionally excluded
  from the "matched % of authorable code" headline.

NOTE for Lane A (sync_match_percent.py):  that script currently carries its own
  ``SDK_UNIT_PREFIXES = ["default/xdk/"]`` constant.  A follow-up should update
  it to ``from scripts.authorable import SDK_UNIT_PREFIXES`` so both callers
  stay in sync.  The addition of ``default/lib/binkxenon/`` to this list is the
  only substantive change vs the current sync_match_percent.py constant.
"""

from __future__ import annotations

# Unit-name prefixes (as they appear in report.json ``units[].name``) that
# identify vendor/SDK code which has no C++ source in the repo and must not
# count toward the authorable matched-% denominator.
SDK_UNIT_PREFIXES: list[str] = [
    "default/xdk/",           # Microsoft Xbox Dev Kit
    "default/lib/binkxenon/", # RAD Game Tools Bink video codec
]


def is_authorable(unit_name: str) -> bool:
    """Return True when *unit_name* belongs to authorable (non-SDK) code.

    Parameters
    ----------
    unit_name:
        The ``name`` field of a report.json unit entry, e.g.
        ``"default/system/rndobj/Mesh"`` or ``"default/xdk/d3d9i/..."``.
    """
    return not any(unit_name.startswith(prefix) for prefix in SDK_UNIT_PREFIXES)
