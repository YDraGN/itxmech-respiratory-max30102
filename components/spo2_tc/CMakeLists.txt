# Thư viện quyết định cảnh báo + model hybrid_tc, chép từ
# SPO2-RESEARCH/respiratory-risk-classifier/firmware (đồng bộ lại bằng scripts/sync_spo2_tc.sh).
# test/golden_vectors.h chỉ dùng cho self-test lúc boot.
idf_component_register(
    SRCS "src/spo2_tc.c" "model/hybrid_tc_model_data.cc"
    INCLUDE_DIRS "include" "model" "test"
)
