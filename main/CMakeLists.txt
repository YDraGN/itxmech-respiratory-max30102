idf_component_register(
    SRCS
        "app_main.cc"
        "ble/ble_gatt.c"
        "ble/ble_proto.c"
        "link/link_manager.c"
        "link/uplink.c"
        "model/model_runner.cc"
        "sensor/ppg.c"
        "sensor/vitals.c"
        "storage/device_state.c"
        "storage/record_log.c"
        "storage/record_store.c"
        "storage/wifi_list.c"
        "storage/wifi_store.c"
        "system/device_clock.c"
        "system/led_status.c"
        "system/wristband_config.c"
    INCLUDE_DIRS "." "ble" "link" "model" "sensor" "storage" "system"
    REQUIRES spo2_tc max30102 bt nvs_flash esp_timer esp_app_format driver esp_partition
)
