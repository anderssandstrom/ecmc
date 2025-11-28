#!/usr/bin/env bash
set -euo pipefail

# Basic build smoke: runs the project makefile to catch syntax/errors.
# Optional: set JOBS to override parallelism.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RELEASE_FILE="${ROOT_DIR}/../configure/RELEASE"
JOBS=${JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || sysctl -n hw.ncpu 2>/dev/null || echo 4)}

# Try to detect EPICS_BASE from release config; bail with a helpful hint if missing.
EPICS_BASE_CFG=$(awk -F= '/^EPICS_BASE/{gsub(/[ \t]/,"",$2);print $2}' "${RELEASE_FILE}" 2>/dev/null || true)
EPICS_BASE_PATH=${EPICS_BASE:-${EPICS_BASE_CFG}}

if [[ -z "${EPICS_BASE_PATH}" || ! -d "${EPICS_BASE_PATH}" ]]; then
  echo "[smoke] EPICS_BASE not found. Configure your local base by creating ../RELEASE.local with:"
  echo "        EPICS_BASE=/path/to/epics-base"
  echo "      or export EPICS_BASE before running this script."
  exit 2
fi

echo "[smoke] running make -j${JOBS} in ${ROOT_DIR}"
make -C "${ROOT_DIR}" -j"${JOBS}"
