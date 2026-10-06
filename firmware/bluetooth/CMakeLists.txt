cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TrackNodeBluetooth LANGUAGES NONE)
endif()

find_program(TRACK_NODE_PLATFORMIO_EXECUTABLE
    NAMES platformio pio
    HINTS
        "$ENV{PLATFORMIO_CORE_DIR}/penv/Scripts"
        "$ENV{PLATFORMIO_CORE_DIR}/penv/bin"
        "$ENV{USERPROFILE}/.platformio/penv/Scripts"
        "$ENV{HOME}/.platformio/penv/bin"
    DOC "PlatformIO executable used to build the Bluetooth coprocessor firmware"
)

add_custom_target(bluetooth
    COMMAND "${CMAKE_COMMAND}"
        "-DPLATFORMIO=${TRACK_NODE_PLATFORMIO_EXECUTABLE}"
        "-DFIRMWARE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build.cmake"
    SOURCES
        src/composition/main.cpp
        src/platform/arduino_entry.cpp
        src/application/firmware_application.cpp
        src/infrastructure/bridge_service.cpp
        src/drivers/devices/arduino_uart.cpp
        src/drivers/devices/bluefruit_backend.cpp
        src/protocols/bridge_codec.cpp
        include/application/firmware_application.h
        include/infrastructure/bridge_service.h
        include/drivers/uart.h
        include/drivers/bluetooth_backend.h
        include/drivers/devices/arduino_uart.h
        include/drivers/devices/bluefruit_backend.h
        include/protocols/bridge_frame.h
        include/protocols/bridge_codec.h
        include/common/buffer.h
        include/common/bluetooth_types.h
        include/core/result.h
        include/composition/firmware.h
        platformio.ini
    USES_TERMINAL
    VERBATIM
)
