cmake_minimum_required(VERSION 3.24)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(TrackNodeBluetooth LANGUAGES C CXX ASM)

target_sources(app PRIVATE
    src/platform/zephyr_entry.cpp
    src/composition/main.cpp
    src/application/firmware_application.cpp
    src/infrastructure/bridge_service.cpp
    src/drivers/devices/zephyr_uart.cpp
    src/drivers/devices/zephyr_bluetooth.cpp
    src/protocols/bridge_codec.cpp
)

target_include_directories(app PRIVATE
    include
    ../../include
    ../../lib/expected/include
)
