#!/bin/bash
# probe.sh <name> [extra cl flags...]
# Compiles <name>.cpp with the project's exact Xenon MSVC command line (build.ninja
# rule `msvc`: /nologo /wd4355 /wd4164 /c /GR /O1 /Oi /EHsc /TP), plus /FAs, and
# prints the frame size and every stack-slot symbol the listing defines
# (`a$2538 = 80 ; size = 256`) so slot sharing is visible directly.
set -u
name=$1; shift
here=$(cd "$(dirname "$0")" && pwd)
cd "$here" || exit 1
rm -f "$name.asm" "$name.obj"
/home/free/code/milohax/wibo/build/release/wibo WIBO_COMPUTER_NAME=9QVZU3 WIBO_FS_CACHE=1 \
  "WIBO_PATH_MAP=x:/aaa/=$here" \
  /home/free/code/milohax/dc3-decomp/build/compilers/X360/16.00.11886.00/cl.exe \
  /nologo /wd4355 /wd4164 /c /GR /O1 /Oi /EHsc /TP "$@" /FAs "/Fa$name.asm" "/Fo$name.obj" "$name.cpp" 2>&1 | grep -v "^$name.cpp$"
[ -f "$name.asm" ] || { echo "NO LISTING"; exit 2; }
echo "== $name  flags: $*"
grep -E "^\s+stwu\s+r1," "$name.asm" | head -1
grep -E '^[A-Za-z_][A-Za-z0-9_]*[$][0-9]* = ' "$name.asm"
