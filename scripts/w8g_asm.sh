#!/bin/zsh
# Standalone /FAsc compile of ONE source file through wibo, for fast
# source-spelling iteration without paying a full ninja.
#
#   scripts/w8g_asm.sh system/utl/trie.cpp trie      # -> /tmp/trie.asm
#
# The flags mirror the ninja `msvc` rule minus the PCH, and WIBO_PATH_MAP is
# load-bearing: without it cl.exe cannot resolve `#include "obj/Task.h"` and
# dies with C1083.  Numbers taken from the listing are for ITERATION ONLY --
# quote every percentage from a full `ninja` + report.json.
REPO=${REPO_ROOT:-${0:a:h:h}}
# wibo: prefer this tree's symlinked toolchain, else the sibling checkout.
WIBO=$REPO/build/tools/wibo
[[ -x $WIBO ]] || WIBO=$REPO/../wibo/build/release/wibo
[[ -x $WIBO ]] || { print -u2 "w8g_asm.sh: no wibo at $REPO/build/tools/wibo or $REPO/../wibo/build/release/wibo"; exit 9 }
REL="$1"; OUT="$2"
cd $REPO/src/$REL:h
"$WIBO" WIBO_COMPUTER_NAME='9QVZU3' WIBO_FS_CACHE='1' \
 WIBO_PATH_MAP="e:/lazer_build_gmc1/system/src/=$REPO/src/system;e:/lazer_build_gmc1/lazer/src/=$REPO/src/lazer" \
 $REPO/build/compilers/X360/16.00.11886.00/cl.exe \
 /I 'e:\lazer_build_gmc1\system\src\stlport' /I $REPO/src/xdk/LIBCMT \
 /I 'e:\lazer_build_gmc1\system\src' /I 'e:\lazer_build_gmc1\lazer\src' \
 /I 'e:\lazer_build_gmc1\system\src\oggvorbis' /I 'e:\lazer_build_gmc1\system\src\synth\tomcrypt' \
 /I 'e:\lazer_build_gmc1\system\src\net\curl\include' /I $REPO/src \
 /nologo /wd4355 /wd4164 /c /GR /O1 /Oi /EHsc /TP /FAsc /Fa/tmp/$OUT.asm /Fo/tmp/$OUT.obj ${REL:t} 2>&1 | tail -3
