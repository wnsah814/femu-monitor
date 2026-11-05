#!/usr/bin/env bash
#
# setup.sh — create the FEMU build tree
#
# This repository tracks only our changes to FEMU (femu/hw/femu). setup.sh
# copies the pinned upstream FEMU (the FEMU submodule) into build/ and links
# build/hw/femu to the overlay, so edits under femu/hw/femu are picked up by
# the next make in build/.
#
# Usage: ./setup.sh
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
UPSTREAM="$ROOT/FEMU"
OVERLAY="$ROOT/femu/hw/femu"
BUILD="$ROOT/build"

[[ -f $UPSTREAM/VERSION ]] || {
  echo "The FEMU submodule is empty. Run first: git submodule update --init FEMU"; exit 1; }

echo "[1/2] copy upstream FEMU to build/ (except hw/femu)"
mkdir -p "$BUILD"
rsync -a --delete --exclude=.git --exclude=/hw/femu --exclude="/build-femu*" \
  "$UPSTREAM/" "$BUILD/"

echo "[2/2] link build/hw/femu to the overlay"
rm -rf "$BUILD/hw/femu"
ln -s "$OVERLAY" "$BUILD/hw/femu"

echo "Done. To compile:"
echo "  cd $BUILD && mkdir -p build-femu && cd build-femu"
echo "  ../configure --enable-kvm --target-list=x86_64-softmmu"
echo "  make -j\$(nproc)"
echo "(dependencies: $OVERLAY/scripts/pkgdep.sh)"
