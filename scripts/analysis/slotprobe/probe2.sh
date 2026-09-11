#!/bin/bash
# probe2.sh <name> [extra flags] -- like probe.sh but with the project's include paths (real headers, no PCH)
set -u
name=$1; shift
here=$(cd "$(dirname "$0")" && pwd)
wt=$(cd "$here/../../.." && pwd)
cd "$here" || exit 1
rm -f "$name.asm" "$name.obj"
/home/free/code/milohax/wibo/build/release/wibo WIBO_COMPUTER_NAME=9QVZU3 WIBO_FS_CACHE=1 \
  "WIBO_PATH_MAP=e:/lazer_build_gmc1/system/src/=$wt/src/system;e:/lazer_build_gmc1/lazer/src/=$wt/src/lazer;x:/aaa/=$here" \
  /home/free/code/milohax/dc3-decomp/build/compilers/X360/16.00.11886.00/cl.exe \
  /I 'e:\lazer_build_gmc1\system\src\stlport' /I "$wt/src/xdk/LIBCMT" /I 'e:\lazer_build_gmc1\system\src' /I 'e:\lazer_build_gmc1\lazer\src' /I "$wt/src" \
  /nologo /wd4355 /wd4164 /c /GR /O1 /Oi /EHsc /TP "$@" /FAs "/Fa$name.asm" "/Fo$name.obj" "$name.cpp" 2>&1 | grep -v "^$name.cpp$"
[ -f "$name.asm" ] || { echo "NO LISTING"; exit 2; }
echo "== $name  flags: $*"
awk '/PROC NEAR/{p=1} p && /stwu[ \t]+r1,/{print; exit}' "$name.asm"
grep -E '^[A-Za-z_][A-Za-z0-9_]*\$[0-9]+ = ' "$name.asm"
