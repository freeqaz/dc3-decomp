#!/usr/bin/env bash
# Import one Xbox 360 DLC package (an STFS/LIVE file) for the native port.
#
#   scripts/dlc/import.sh <package> [dlc_root]
#
# Verifies the package's whole SHA-1 hash tree, then extracts it, lower-cased,
# to <dlc_root>/<package file name, lower-cased>/ -- one directory per package,
# which is what native/src/platform/ContentMgr_Native.cpp mounts. dlc_root
# defaults to orig-assets/dlc in this checkout (gitignored: game assets are
# never committed). The package itself is only read.
#
# Play it:  DC3_DLC_DIR=<dlc_root> native/build/dc3-native
# (DC3_DLC_DIR takes ':'-separated roots; every subdirectory is a package.)
#
# Exit codes: 0 imported, 1 usage or destination problem, 2 not an STFS
# package, 3 the package failed hash verification (nothing is installed).
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 || $1 == -h || $1 == --help ]]; then
    sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
package=$1
dlc_root=${2:-$repo/orig-assets/dlc}

if [[ ! -f $package ]]; then
    echo "import.sh: no such package file: $package" >&2
    exit 1
fi

name=$(basename "$package" | tr '[:upper:]' '[:lower:]')
dest=$dlc_root/$name
if [[ -e $dest ]]; then
    echo "import.sh: $dest already exists; move it away to re-import" >&2
    exit 1
fi

python3 -I "$here/stfs_extract.py" info "$package"
mkdir -p "$dlc_root"
staging=$(mktemp -d "$dlc_root/.import-$name.XXXXXX")
set +e
python3 -I "$here/stfs_extract.py" extract "$package" "$staging" --lowercase
rc=$?
set -e
if [[ $rc -ne 0 ]]; then
    echo "import.sh: extraction failed (exit $rc); the partial copy is left in $staging" >&2
    exit "$rc"
fi
if [[ ! -f $staging/songs/songs.dta ]]; then
    echo "import.sh: warning: $name has no songs/songs.dta; the song manager will not see it" >&2
fi
mv "$staging" "$dest"
n=$(find "$dest" -type f | wc -l)
echo "imported $name: $n files in $dest"
echo "play it: DC3_DLC_DIR=$dlc_root native/build/dc3-native"
