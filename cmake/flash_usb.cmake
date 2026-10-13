if(NOT EXISTS "${PROGRAMMER}" OR NOT EXISTS "${FIRMWARE}")
    message(FATAL_ERROR "Set PROGRAMMER and FIRMWARE to existing files")
endif()

execute_process(
    COMMAND "${PROGRAMMER}" -l usb
    RESULT_VARIABLE list_result
    OUTPUT_VARIABLE devices
    ERROR_VARIABLE list_error
)
if(NOT list_result EQUAL 0)
    message(FATAL_ERROR "DFU enumeration failed: ${devices}${list_error}")
endif()
string(REGEX MATCHALL "Device Index[ \t]*:[ \t]*USB[0-9]+" ports "${devices}")
list(LENGTH ports count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "Expected one STM32 DFU device, found ${count}. Enter BOOT0/RESET mode and disconnect other DFU boards.\n${devices}")
endif()
if(NOT devices MATCHES "Device ID[ \t]*:[ \t]*0x0?431([^0-9A-Fa-f]|$)")
    message(FATAL_ERROR "Connected DFU device is not STM32F411 (ID 0x431).\n${devices}")
endif()
string(REGEX REPLACE ".*:[ \t]*(USB[0-9]+)" "\\1" port "${ports}")
string(REGEX MATCH "Serial number[ \t]*:[ \t]*([0-9A-Fa-f]+)" serial_match "${devices}")
if(NOT serial_match)
    message(FATAL_ERROR "DFU device serial number is missing.\n${devices}")
endif()
set(serial "${CMAKE_MATCH_1}")
message(STATUS "Programming STM32F411 ${serial} via ${port}: ${FIRMWARE}")

execute_process(
    COMMAND "${PROGRAMMER}" -c "port=${port}" "sn=${serial}" -w "${FIRMWARE}" -v
    RESULT_VARIABLE flash_result
)
if(NOT flash_result EQUAL 0)
    message(FATAL_ERROR "USB download/verification failed; application was not started")
endif()

execute_process(
    COMMAND "${PROGRAMMER}" -c "port=${port}" "sn=${serial}" -s 0x08000000
    RESULT_VARIABLE start_result
)
if(NOT start_result EQUAL 0)
    message(FATAL_ERROR "Firmware was written and verified, but USB start failed. Release BOOT0 and press RESET")
endif()
