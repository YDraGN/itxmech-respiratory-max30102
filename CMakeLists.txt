cmake_minimum_required(VERSION 3.16)

# Phiên bản firmware: hiện trong DEVICE_INFO (khóa "fw") qua esp_app_get_description().
set(PROJECT_VER "0.2.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(itxmech_wristband)
