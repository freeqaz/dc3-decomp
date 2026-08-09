#!/usr/bin/env python3
"""Shared write path for the five post-compile `.obj` patchers.

Why this exists
---------------
The patchers rewrite `build/<version>/src/**/*.obj` **in place** -- files that
ninja itself produced.  A naive `open(path, 'wb')` bumps the object's mtime
*after* ninja recorded that object's `/showIncludes` header dependencies in
`.ninja_deps`, and ninja treats a deps entry whose output is newer than the
recorded mtime as unusable:

    ninja explain: stored deps info out of date for 'build/.../Foo.obj'

"deps missing" means "dirty", so the **next** `ninja` recompiles every object a
patcher touched -- measured 2026-08-09 at **277 of 980** on dc3 `21f7f331`.
That recompile is the whole failure: it produces a fresh, *unpatched* object,
and the patch stamps do not re-run behind it unless the graph says they must.

Preserving the mtime tells ninja the truth it needs: the compile edge's output
is final as of the compile, and the deps it recorded for it are still valid.
The alternative -- letting the mtime move and re-running the patchers on every
build (which the `implicit: all_source` edge in `configure.py` would then do)
-- is *correct* but never converges: every single `ninja` recompiles those 277
objects and re-patches them, forever, and `ninja` can never say "no work to
do", so a clean tree stops being distinguishable from a dirty one.

What this trades away
---------------------
An object's CONTENT changes here without its mtime changing.  That is invisible
to any mtime-keyed cache -- but the only window in which it matters is between
the compile edge and the patch edge **inside one ninja invocation**, and every
consumer in the build (`report.json`, objdiff, the link) is ordered after
`post-compile`.  Consumers in a later invocation only ever see the finished,
patched file.

It also means the tree's patch state cannot be established from timestamps at
all -- which is exactly why `scripts/verify_objs_patched.py` exists and is
wired into the default build.  Do not replace that check with an mtime rule.

Gate (k) of the decomp-synth ADDR_IDENTITY witness (object-older-than-source)
is unaffected: the preserved mtime is the COMPILE time, which still postdates
the source.
"""

import os


def write_patched_obj(path, data) -> None:
    """Rewrite `path` with `data`, restoring its original mtime/atime.

    See the module docstring: the mtime restore is load-bearing for ninja's
    `.ninja_deps` validity, not a cosmetic nicety.
    """
    st = os.stat(path)
    with open(path, 'wb') as f:
        f.write(data)
    os.utime(path, ns=(st.st_atime_ns, st.st_mtime_ns))
