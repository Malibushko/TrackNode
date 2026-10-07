set(TRACK_NODE_BLUETOOTH_SDK_ROOT "" CACHE PATH "nRF Connect SDK west workspace")
set(TRACK_NODE_BLUETOOTH_BOARD "xiao_ble/nrf52840" CACHE STRING "Zephyr Bluetooth board")
set(TRACK_NODE_BLUETOOTH_GNUARMEMB_ROOT "" CACHE PATH "Optional GNU Arm Embedded toolchain root")
find_program(TRACK_NODE_WEST_EXECUTABLE NAMES west DOC "west from the Nordic toolchain environment")

add_custom_target(bluetooth
    COMMAND "${CMAKE_COMMAND}"
        "-DWEST=${TRACK_NODE_WEST_EXECUTABLE}"
        "-DSDK_ROOT=${TRACK_NODE_BLUETOOTH_SDK_ROOT}"
        "-DBOARD=${TRACK_NODE_BLUETOOTH_BOARD}"
        "-DGNUARMEMB_ROOT=${TRACK_NODE_BLUETOOTH_GNUARMEMB_ROOT}"
        "-DNINJA=${CMAKE_MAKE_PROGRAM}"
        "-DFIRMWARE_DIR=${PROJECT_SOURCE_DIR}/firmware/bluetooth"
        "-DBUILD_DIR=${PROJECT_BINARY_DIR}/bluetooth"
        -P "${PROJECT_SOURCE_DIR}/firmware/bluetooth/cmake/build.cmake"
    SOURCES
        "${PROJECT_SOURCE_DIR}/firmware/bluetooth/CMakeLists.txt"
        "${PROJECT_SOURCE_DIR}/firmware/bluetooth/prj.conf"
        "${PROJECT_SOURCE_DIR}/firmware/bluetooth/app.overlay"
        "${PROJECT_SOURCE_DIR}/firmware/bluetooth/src/main.cpp"
        "${PROJECT_SOURCE_DIR}/firmware/bluetooth/src/drivers/devices/zephyr_uart.cpp"
        "${PROJECT_SOURCE_DIR}/firmware/bluetooth/src/drivers/devices/zephyr_bluetooth.cpp"
        "${PROJECT_SOURCE_DIR}/include/drivers/protocols/ble_bridge.h"
    USES_TERMINAL
    VERBATIM
)
