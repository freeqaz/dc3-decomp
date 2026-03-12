#!/usr/bin/env bash

# Source this file to activate the local venv and shared permuter DB root.
# Usage:
#   source scripts/setup-env.sh

_dc3_setup_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_dc3_repo_root="$(cd "${_dc3_setup_dir}/.." && pwd)"

export PERMUTER_DB_ROOT="${_dc3_repo_root}"
export MILOHAX_DIR="$(cd "${_dc3_repo_root}/.." && pwd)"

# Create venv and install deps if missing
if [ ! -f "${_dc3_repo_root}/venv/bin/activate" ]; then
  echo "setup-env.sh: creating venv at ${_dc3_repo_root}/venv..."
  python3 -m venv "${_dc3_repo_root}/venv"
  # shellcheck disable=SC1091
  source "${_dc3_repo_root}/venv/bin/activate"
  pip install --quiet --upgrade pip
  pip install --quiet -r "${_dc3_repo_root}/requirements.txt"
else
  # shellcheck disable=SC1091
  source "${_dc3_repo_root}/venv/bin/activate"
fi

unset _dc3_setup_dir
unset _dc3_repo_root
