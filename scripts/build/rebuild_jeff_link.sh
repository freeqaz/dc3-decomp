#!/bin/bash
# Rebuild jeff (dtk), re-split XEX objects, and link.
# Shows summary of linker errors/warnings.
#
# Usage: scripts/build/rebuild_jeff_link.sh
set -e

JEFF_DIR=~/code/milohax/jeff
DC3_DIR=~/code/milohax/dc3-decomp
LINK_LOG=/tmp/claude/link_output.txt

echo "=== Building jeff ==="
cd "$JEFF_DIR"
cargo build --release 2>&1 | tail -3

echo "=== Reconfiguring ==="
cd "$DC3_DIR"
python3 configure.py \
    --dtk "$JEFF_DIR/target/release/dtk" \
    --objdiff ../objdiff/target/release/objdiff-cli \
    --wibo ../wibo/build/release/wibo 2>&1

echo "=== Cleaning split objects ==="
rm -rf build/373307D9/obj

echo "=== Building + Linking ==="
mkdir -p /tmp/claude
ninja link 2>&1 > "$LINK_LOG" || true

LNK4006=$(grep -c "LNK4006" "$LINK_LOG" || true)
LNK1169=$(grep -c "LNK1169" "$LINK_LOG" || true)
LNK1223=$(grep -c "LNK1223" "$LINK_LOG" || true)
LNK2013=$(grep -c "LNK2013" "$LINK_LOG" || true)
ERRORS=$(grep -c " error LNK" "$LINK_LOG" || true)

echo ""
echo "=== Results ==="
echo "LNK4006 (duplicate symbol): $LNK4006"
echo "LNK1169 (multiply defined): $LNK1169"
echo "LNK1223 (invalid pdata):    $LNK1223"
echo "LNK2013 (fixup overflow):   $LNK2013"
echo "Total errors:               $ERRORS"
echo ""
echo "Full output: $LINK_LOG"
