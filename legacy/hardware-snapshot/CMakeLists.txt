set(FW ${CMAKE_CURRENT_LIST_DIR}/../../..)
idf_component_register(
    SRCS "main.cc" "model_runner.cc" "max30102.c" "${FW}/src/spo2_tc.c" "${FW}/model/hybrid_tc_model_data.cc"
    INCLUDE_DIRS "." "${FW}/include" "${FW}/model" "${FW}/test"
)
