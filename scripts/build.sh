#!/usr/bin/env sh
# Builds Stack Tower with ufbt and copies the .fap to the repository root.
# Usage:  ./scripts/build.sh
set -e
cd "$(dirname "$0")/.."
python3 -m ufbt
cp dist/stack_tower.fap stack_tower.fap
echo "OK - stack_tower.fap is ready (copy it to SD Card/apps/Games/)."
