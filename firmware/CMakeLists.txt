# DeskPet Agent 固件 Demo — 顶层 CMake
# ESP32-S3 + 466x466 圆形 AMOLED (SH8601 QSPI) + FT5x06 触摸 + LVGL 9
cmake_minimum_required(VERSION 3.16)

# 抑制第三方组件的字段初始化告警
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PROJECT_VER "0.1.0")
project(desk_pet_agent)
