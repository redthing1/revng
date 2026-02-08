#!/usr/bin/env bash
# This file is distributed under the MIT License. See LICENSE.md for details.

set -euo pipefail

SDK_IMPORT="${1:?sdk-import path}"
SDK_LIFT="${2:?sdk-lift path}"
SDK_CFG="${3:?sdk-cfg path}"
SDK_ISOLATE="${4:?sdk-isolate path}"
SDK_DECOMPILE="${5:?sdk-decompile path}"
FIXTURE_BIN="${6:?fixture binary path}"
OUT_DIR="${7:?output directory}"

mkdir -p "${OUT_DIR}"

MODEL_YML="${OUT_DIR}/model.yml"
LIFT_BC="${OUT_DIR}/lift.bc"
CFG_TAR="${OUT_DIR}/cfg.tar.gz"
ISOLATED_TAR="${OUT_DIR}/isolated.tar.gz"
DECOMPILED_TAR="${OUT_DIR}/decompiled.tar.gz"
EXTRACT_DIR="${OUT_DIR}/extracted"

"${SDK_IMPORT}" -o "${MODEL_YML}" "${FIXTURE_BIN}"
test -s "${MODEL_YML}"

ENTRY="$(awk '/^EntryPoint:/{print $2; exit}' "${MODEL_YML}" | tr -d '\"')"
test -n "${ENTRY}"

SAFE="${ENTRY//:/_}"
SAFE="${SAFE//\//_}"

"${SDK_LIFT}" -o "${LIFT_BC}" "${FIXTURE_BIN}"
test -s "${LIFT_BC}"

"${SDK_CFG}" --function-entry "${ENTRY}" -o "${CFG_TAR}" "${FIXTURE_BIN}"
tar tzf "${CFG_TAR}" | grep -qx "cfg/${SAFE}.yml"

"${SDK_ISOLATE}" --function-entry "${ENTRY}" -o "${ISOLATED_TAR}" "${FIXTURE_BIN}"
tar tzf "${ISOLATED_TAR}" | grep -qx "isolated/${SAFE}.bc"

"${SDK_DECOMPILE}" --function-entry "${ENTRY}" -o "${DECOMPILED_TAR}" "${FIXTURE_BIN}"
tar tzf "${DECOMPILED_TAR}" | grep -qx "decompiled/functions.c"

mkdir -p "${EXTRACT_DIR}"
tar xzf "${DECOMPILED_TAR}" -C "${EXTRACT_DIR}"

cc -c -std=c11 -I"${EXTRACT_DIR}/decompiled" \
  "${EXTRACT_DIR}/decompiled/functions.c" \
  -o "${OUT_DIR}/functions.o"
test -s "${OUT_DIR}/functions.o"

