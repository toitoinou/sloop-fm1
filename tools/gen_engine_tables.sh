#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The engine's generated headers (wave tables, samples, drum kits, FM6 patches) for the app, made by the
# generators SLOOP's firmware used (tools/gen_*.py).
#   tools/gen_engine_tables.sh [out-dir]      (default: engine/gen)
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/engine/gen}"
mkdir -p "$OUT"
cd "$ROOT"
python3 tools/gen_tables.py "$OUT/felucca_tables.h"
python3 tools/gen_samples.py "$OUT/felucca_samples.h"
python3 tools/gen_drumkits.py "$OUT/felucca_drumkits.h"
python3 tools/gen_fm6_patches.py "$OUT/felucca_fm6.h"
