#!/usr/bin/env bash
# Chép lại thư viện spo2_tc + model + golden vectors từ repo nghiên cứu sau khi export model mới.
#   bash scripts/sync_spo2_tc.sh [đường_dẫn_tới/respiratory-risk-classifier/firmware]
# Sau khi chạy: build lại, kiểm tra log "self-test: 24/24" và SPO2TC_MODEL_VERSION.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/../SPO2-RESEARCH/respiratory-risk-classifier/firmware}"
DST="$ROOT/components/spo2_tc"

for f in include/spo2_tc.h include/spo2_tc_params.h src/spo2_tc.c \
         model/hybrid_tc_model_data.cc model/hybrid_tc_model_data.h test/golden_vectors.h; do
  test -f "$SRC/$f" || { echo "Thiếu $SRC/$f" >&2; exit 1; }
done

cp "$SRC/include/spo2_tc.h" "$SRC/include/spo2_tc_params.h" "$DST/include/"
cp "$SRC/src/spo2_tc.c" "$DST/src/"
cp "$SRC/model/hybrid_tc_model_data.cc" "$SRC/model/hybrid_tc_model_data.h" "$DST/model/"
cp "$SRC/test/golden_vectors.h" "$DST/test/"
cp "$SRC/examples/esp32c3/main/model_runner.cc" "$SRC/examples/esp32c3/main/model_runner.h" "$ROOT/main/model/"

grep -m1 "SPO2TC_MODEL_VERSION" "$DST/include/spo2_tc_params.h"
echo "Đã đồng bộ từ $SRC"
