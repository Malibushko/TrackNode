if(NOT PLATFORMIO OR PLATFORMIO MATCHES "-NOTFOUND$")
    message(FATAL_ERROR "Install PlatformIO or configure TRACK_NODE_PLATFORMIO_EXECUTABLE with its executable path")
endif()

execute_process(
    COMMAND "${PLATFORMIO}" run --project-dir "${FIRMWARE_DIR}" --environment xiao_nrf52840
    RESULT_VARIABLE build_result
)

if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Bluetooth firmware build failed: ${build_result}")
endif()
