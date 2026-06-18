#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-rv"
DPU_BIN="${BUILD_DIR}/devApp/rvbins/LLAMA_GEMV_F32"

cmake --build "${BUILD_DIR}" --target rvLLAMA_GEMV_F32 dmmLLAMA_GEMV_F32 -j"$(nproc)"

if [[ ! -x "${DPU_BIN}" ]]; then
  echo "missing DPU binary: ${DPU_BIN}" >&2
  exit 1
fi

DMM_NR_SIM_THRDS="${DMM_NR_SIM_THRDS:-4}" \
  "${BUILD_DIR}/dmmLLAMA_GEMV_F32" "${DPU_BIN}"
