#!/bin/bash
# dis.sh <key> <ps|vs> <outdir> : disassemble a cached standard shader, skipping a leading literal table
X=/home/free/code/milohax/xenia/build/bin/Linux/Release/xenia-gpu-shader-compiler
S=/home/free/code/milohax/dc3-decomp/archive/np-gfx-2026-11-14/lanes/light/shaders/standard
k=$1; t=$2; O=$3; f=$S/standard_$k.$t
best=0; bestoff=-1
for off in 0 16 32 48 64 80 96 112 128 144 160 176 192 208 224 240 256; do
  tail -c +$((off+1)) $f > $O/.tmp.bin
  $X --shader_input=$O/.tmp.bin --shader_input_type=$t --shader_output=$O/.tmp.txt --shader_output_type=ucode >/dev/null 2>&1
  n=$(grep -c . $O/.tmp.txt 2>/dev/null); n=${n:-0}
  if [ "$n" -gt "$best" ] && head -1 $O/.tmp.txt | grep -q exec; then best=$n; bestoff=$off; cp $O/.tmp.txt $O/$k.$t.txt; fi
done
echo "$k.$t offset=$bestoff lines=$best"
[ $bestoff -gt 0 ] && od -An -tx4 --endian=big -N $bestoff $f | tr -s ' ' '\n' | grep . | python3 -c "import sys,struct;print('literals:',[round(struct.unpack('>f',bytes.fromhex(l.strip()))[0],4) for l in sys.stdin])"
