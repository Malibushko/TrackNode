cmake_minimum_required(VERSION 3.24)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(TrackNodeBluetooth LANGUAGES C CXX ASM)

target_sources(app PRIVATE
    src/main.cpp
    src/infrastructure/bridge_service.cpp
    src/drivers/devices/zephyr_uart.cpp
    src/drivers/devices/zephyr_bluetooth.cpp
    src/drivers/devices/zephyr_gatt_server.cpp
    ../../src/drivers/protocols/bridge_codec.cpp
    ../../src/drivers/protocols/bridge_stream_parser.cpp
)

target_include_directories(app PRIVATE
    include
    ../../include
    ../../lib/expected/include
)
