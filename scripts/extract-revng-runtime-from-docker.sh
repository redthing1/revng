#!/usr/bin/env bash
#
# This file is distributed under the MIT License. See LICENSE.md for details.
#
# Extract the revng runtime/deps prefix from a pinned Docker image.
#
# The official revng image contains an installation prefix at /revng/root.
# This script copies that prefix to a host directory, so it can be used as a
# CMAKE_PREFIX_PATH / deps prefix for building revng (or consumers).
#
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage:
  extract-revng-runtime-from-docker.sh <image> <out_dir>

Examples:
  ./scripts/extract-revng-runtime-from-docker.sh \
    revng/revng@sha256:... \
    /tmp/revng-runtime

Notes:
  - The output directory must not exist (or must be empty).
  - The script extracts /revng/root from the container.
EOF
  exit 2
}

if [[ $# -ne 2 ]]; then
  usage
fi

IMAGE="$1"
OUT_DIR="$2"

if ! command -v docker >/dev/null 2>&1; then
  echo "error: docker not found in PATH" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
if [[ -n "$(ls -A "$OUT_DIR" 2>/dev/null || true)" ]]; then
  echo "error: output directory is not empty: $OUT_DIR" >&2
  exit 1
fi

PARENT_DIR="$(cd "$(dirname "$OUT_DIR")" && pwd -P)"
BASE_NAME="$(basename "$OUT_DIR")"
TMP_DIR="${PARENT_DIR}/.${BASE_NAME}.tmp.$$"

mkdir -p "$TMP_DIR"

CID=""
CID="$(docker create "$IMAGE")"
cleanup_all() {
  if [[ -n "${CID}" ]]; then
    docker rm -f "$CID" >/dev/null 2>&1 || true
  fi
  rm -rf "$TMP_DIR" >/dev/null 2>&1 || true
}
trap cleanup_all EXIT

docker cp "${CID}:/revng/root/." "$TMP_DIR/"

# Record provenance for reproducibility.
printf '%s\n' "$IMAGE" > "${TMP_DIR}/.revng_runtime_image"

rm -rf "$OUT_DIR"
mv -T "$TMP_DIR" "$OUT_DIR"
