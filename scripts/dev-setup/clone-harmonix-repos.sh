#!/usr/bin/env bash
set -euo pipefail

# Clone Harmonix / Rock Band / Dance Central related repos over SSH.
#
# This is a local copy of milo-engine-libs/clone.sh, adapted to work
# standalone from the dc3-decomp repo. It creates the full directory
# structure under ~/code/milohax/milo-engine-libs/harmonix-repos/.
#
# Assumes:
#   1) You have an SSH key added to GitHub
#   2) "git" is installed
#
# Usage:
#   ./scripts/dev-setup/clone-harmonix-repos.sh
#
# Override destination:
#   DEST=~/src/harmonix ./scripts/dev-setup/clone-harmonix-repos.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MILOHAX_DIR="$(cd "$REPO_ROOT/.." && pwd)"

DEST="${DEST:-$MILOHAX_DIR/milo-engine-libs/harmonix-repos}"
mkdir -p "$DEST"
cd "$DEST"

repos=(
  "git@github.com:ihatecompvir/MiloEditor.git"
  "git@github.com:PikminGuts92/Mackiloha.git"
  "git@github.com:VelocityRa/awesome-game-file-format-reversing.git"
  "git@github.com:maxton/DtxCS.git"
  "git@github.com:mtolly/dtab.git"
  "git@github.com:Deimos/dtb2dta.git"
  "git@github.com:hmxmilohax/milo-script-library.git"
  "git@github.com:maxton/GameArchives.git"
  "git@github.com:NORXND/Boomy.git"
  "git@github.com:PikminGuts92/pikaxe.git"
  "git@github.com:hmxmilohax/milo-rnd-library.git"
  "git@github.com:PikminGuts92/PyMilo.git"
  "git@github.com:maxton/LibForge.git"
)

for repo in "${repos[@]}"; do
  name="$(basename "$repo" .git)"

  if [[ -d "$name/.git" ]]; then
    echo "==> $name already exists, skipping"
    continue
  fi

  echo "==> Cloning $repo"
  git clone "$repo"
done

echo
echo "Done. Cloned repos into: $DEST"
