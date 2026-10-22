#!/usr/bin/env bash
# Chạy test phần C thuần (giao thức BLE, bộ đệm flash) trên máy tính, không cần ESP-IDF.
#   bash tests/host/run.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tests/host/build"
mkdir -p "$OUT"
CC="${CC:-gcc}"
FLAGS=(-std=c11 -Wall -Wextra -Werror -O1 -g
       -I"$ROOT/main/ble" -I"$ROOT/main/storage" -I"$ROOT/main/system"
       -I"$ROOT/components/spo2_tc/include" -I"$ROOT/tests/host")

"$CC" "${FLAGS[@]}" -o "$OUT/test_ble_proto" "$ROOT/tests/host/test_ble_proto.c" \
  "$ROOT/main/ble/ble_proto.c" "$ROOT/main/storage/wifi_list.c" "$ROOT/components/spo2_tc/src/spo2_tc.c" -lm
"$CC" "${FLAGS[@]}" -o "$OUT/test_record_log" "$ROOT/tests/host/test_record_log.c" \
  "$ROOT/main/storage/record_log.c"

"$OUT/test_ble_proto"
"$OUT/test_record_log"
echo "Tất cả test host đều đạt."
