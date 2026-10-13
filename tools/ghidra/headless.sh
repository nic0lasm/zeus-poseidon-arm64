#!/bin/sh
# Lance Ghidra sans interface, en gardant tous ses fichiers dans le projet
# (réglages et cache compris, qui iraient sinon dans le dossier personnel).
#
# Usage : tools/ghidra/headless.sh PROJET [arguments d'analyzeHeadless...]
# Exemple : tools/ghidra/headless.sh zeus -import original/cab-poseidon/Zeus.exe
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GHIDRA_HOME="${GHIDRA_HOME:-/opt/homebrew/opt/ghidra/libexec}"
export JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}"
export XDG_CONFIG_HOME="$ROOT/analysis/ghidra/.config"
export XDG_CACHE_HOME="$ROOT/analysis/ghidra/.cache"

project="$1"
shift
mkdir -p "$ROOT/analysis/ghidra/projects" "$ROOT/analysis/ghidra/export" \
    "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
cd "$ROOT"
exec "$GHIDRA_HOME/support/analyzeHeadless" "$ROOT/analysis/ghidra/projects" "$project" \
    -scriptPath "$ROOT/tools/ghidra" "$@"
