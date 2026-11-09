#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build/host/u55c_bench"
CSV="$ROOT_DIR/results/correctness.csv"
PYTHON_BIN="$ROOT_DIR/.venv/bin/python"

set -a
source "$ROOT_DIR/config/project.env"
set +a

if [[ ! -x "$PYTHON_BIN" ]]; then
  PYTHON_BIN="$(command -v python3)"
fi

RESOLVED_XCLBIN="${U55C_XCLBIN:-}"
if [[ -n "$RESOLVED_XCLBIN" && "$RESOLVED_XCLBIN" != /* ]]; then
  RESOLVED_XCLBIN="$ROOT_DIR/$RESOLVED_XCLBIN"
fi

"$ROOT_DIR/scripts/build_host.sh"

"$BIN" \
  --mode dry-run \
  --kernel "${U55C_KERNEL_NAME:-vector_add}" \
  --device "${U55C_DEVICE_INDEX:-0}" \
  --elements "${U55C_DEFAULT_ELEMENTS:-1048576}" \
  --iterations 3 \
  --csv "$CSV"

"$PYTHON_BIN" "$ROOT_DIR/tests/test_csv_schema.py" "$CSV"

echo "Dry-run correctness: PASS"

if [[ -n "$RESOLVED_XCLBIN" ]]; then
  if [[ ! -f "$RESOLVED_XCLBIN" ]]; then
    echo "WARNING: U55C_XCLBIN is set but file is missing: $RESOLVED_XCLBIN"
    exit 0
  fi

  "$BIN" \
    --mode hw \
    --xclbin "$RESOLVED_XCLBIN" \
    --kernel "${U55C_KERNEL_NAME:-vector_add}" \
    --device "${U55C_DEVICE_INDEX:-0}" \
    --elements "${U55C_DEFAULT_ELEMENTS:-1048576}" \
    --iterations 1 \
    --csv "$CSV"

  echo "Hardware correctness: PASS"
else
  echo "Hardware correctness skipped (set U55C_XCLBIN in config/project.env)."
fi
