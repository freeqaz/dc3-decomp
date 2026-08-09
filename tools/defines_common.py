# Game versions
DEFAULT_VERSION = 0
VERSIONS = [
    "373307D9",  # 0
]

# Include paths
#
# The `e:` roots are spelled with BACKSLASHES, exactly as the retail build
# spelled them, and that is not cosmetic. MSVC names a string literal's COMDAT
# by a hash of the literal's CONTENTS, and `__FILE__` is the path as the
# compiler was handed it -- so an include root reached through `/I e:/...`
# makes every `__FILE__` in every header under it a *different string* from the
# one retail emitted, hence a different symbol, hence a relocation that does
# not match the target even when the instruction bytes are identical.
#
# These were forward slashes until 2026-08-09. Measured cost at that point:
# 1,731 functions whose raw .text already matched the target were refused by
# `raw_text_reloc_eq` on separator-only string constants ALONE -- 43.4% of
# every remaining relocation-only refusal in the repo. Nothing was refused in
# the other direction: no target-side literal spells the root with `/`, so
# there is no function this trades away. Working: the reprobe-v1 readout in
# decomp-synth (`docs/plans/il-witness/REPROBE_V1_2026-08-09.md`).
#
# wibo resolves the include for real, and it normalizes separators before
# consulting WIBO_PATH_MAP, so the map below keeps its forward-slash spelling
# and still matches. The shell does NOT normalize: `project.make_flags_str`
# quotes any flag containing a backslash, because an unquoted one arrives at
# cl.exe as `e:lazer_build_gmc1systemsrc`.
cflags_includes = [
    # C/C++ stdlib
    # STLport requires that it comes first in the include path list
    r"/I e:\lazer_build_gmc1\system\src\stlport",
    "/I src/xdk/LIBCMT",

    # Project source - use absolute mapped paths to match original __FILE__ values
    r"/I e:\lazer_build_gmc1\system\src",
    r"/I e:\lazer_build_gmc1\lazer\src",

    # Libraries
    r"/I e:\lazer_build_gmc1\system\src\oggvorbis",
    r"/I e:\lazer_build_gmc1\system\src\synth\tomcrypt",
    r"/I e:\lazer_build_gmc1\system\src\net\curl\include",

    # Fallback for stlport native CRT include path (xdk/LIBCMT/...)
    # Must come AFTER Windows-mapped paths so headers with __FILE__ resolve correctly
    "/I src",
]
