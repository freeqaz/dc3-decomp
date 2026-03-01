# Game versions
DEFAULT_VERSION = 0
VERSIONS = [
    "373307D9",  # 0
]

# Include paths
cflags_includes = [
    # C/C++ stdlib
    # STLport requires that it comes first in the include path list
    "/I e:/lazer_build_gmc1/system/src/stlport",
    "/I src/xdk/LIBCMT",

    # Project source - use absolute mapped paths to match original __FILE__ values
    # Original build had /I e:\lazer_build_gmc1\system\src and /I e:\lazer_build_gmc1\lazer\src
    "/I e:/lazer_build_gmc1/system/src",
    "/I e:/lazer_build_gmc1/lazer/src",

    # Libraries
    "/I e:/lazer_build_gmc1/system/src/oggvorbis",
    "/I e:/lazer_build_gmc1/system/src/synth/tomcrypt",
    "/I e:/lazer_build_gmc1/system/src/net/curl/include",

    # Fallback for stlport native CRT include path (xdk/LIBCMT/...)
    # Must come AFTER Windows-mapped paths so headers with __FILE__ resolve correctly
    "/I src",
]
