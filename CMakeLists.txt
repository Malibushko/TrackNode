cmake_minimum_required(VERSION 3.24)
project(TrackNode LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FAULT_INJECTION_ENABLE "Inject random faults to simulate real world conditions" OFF)
option(PERF_MEASURING "Log scoped execution times" OFF)
option(TRACK_NODE_TICKLESS_IDLE "Try FreeRTOS tickless idle in the full firmware" OFF)
option(TRACK_NODE_DEBUG_IN_SLEEP "Keep the STM32 debug interface active during Sleep" OFF)
option(NO_STORAGE "Build TrackNode without SD card and filesystem" OFF)

if(CMAKE_CROSSCOMPILING)
    set(STM32CUBE_F4_ROOT "" CACHE PATH "Path to STM32CubeF4 package root")
    if(NOT EXISTS "${STM32CUBE_F4_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal.h")
        message(FATAL_ERROR "Set STM32CUBE_F4_ROOT to the STM32CubeF4 package root")
    endif()

    set(HAL_ROOT "${STM32CUBE_F4_ROOT}/Drivers/STM32F4xx_HAL_Driver")
    set(CMSIS_ROOT "${STM32CUBE_F4_ROOT}/Drivers/CMSIS")
    set(DEVICE_ROOT "${CMSIS_ROOT}/Device/ST/STM32F4xx")

    include(cmake/track_node_options.cmake)

    add_executable(${TRACK_NODE_TARGET}
            include/fault/fault_injection.h
            include/perf.h
            include/drivers/devices/neo6m.h
            include/drivers/protocols/nmea0183.h
            include/drivers/protocols/nmea0183_types.h
            src/drivers/protocols/nmea0183.cpp
            src/drivers/devices/neo6m.cpp
            include/infrastructure/gnss_service.h
            include/drivers/devices/sx126x.h
            include/misc/bits_helpers.h
            include/common/power_mode.h
            include/drivers/board_state_service.h)
    add_subdirectory(src)
    include(cmake/stm32cube_sources.cmake)
    include(cmake/vendor_sources.cmake)

    target_include_directories(${TRACK_NODE_TARGET} PRIVATE ${TRACK_NODE_INCLUDE_DIRECTORIES})
    target_compile_definitions(${TRACK_NODE_TARGET} PRIVATE ${TRACK_NODE_COMPILE_DEFINITIONS})
    target_compile_options(${TRACK_NODE_TARGET} PRIVATE ${TRACK_NODE_COMPILE_OPTIONS})
    target_link_options(${TRACK_NODE_TARGET} PRIVATE ${TRACK_NODE_LINK_OPTIONS})
    add_custom_command(TARGET ${TRACK_NODE_TARGET} POST_BUILD
        COMMAND "${ARM_OBJCOPY}" -O binary "$<TARGET_FILE:${TRACK_NODE_TARGET}>" "${TRACK_NODE_BINARY}"
        COMMAND "${ARM_SIZE}" "$<TARGET_FILE:${TRACK_NODE_TARGET}>"
        VERBATIM
    )

    if(FAULT_INJECTION_ENABLE)
        message(STATUS "Enabling fault injections")
        target_compile_definitions(${TRACK_NODE_TARGET} PRIVATE FAULT_INJECTION_ENABLE=1)
    endif()

    if(PERF_MEASURING)
        message(STATUS "Enable performance measuring")
        target_compile_definitions(${TRACK_NODE_TARGET} PRIVATE PERF_MEASURING=1)
    endif()

    if(TRACK_NODE_TICKLESS_IDLE)
        target_compile_definitions(${TRACK_NODE_TARGET} PRIVATE TRACK_NODE_TICKLESS_IDLE=1)
    endif()

    if(TRACK_NODE_DEBUG_IN_SLEEP)
        target_compile_definitions(${TRACK_NODE_TARGET} PRIVATE TRACK_NODE_DEBUG_IN_SLEEP=1)
    endif()

    if(NO_STORAGE)
        target_compile_definitions(${TRACK_NODE_TARGET} PRIVATE NO_STORAGE=1)
    endif()

    find_program(TRACK_NODE_PROGRAMMER_CLI NAMES STM32_Programmer_CLI)
    if(TRACK_NODE_PROGRAMMER_CLI)
        add_custom_target(flash
            COMMAND "${TRACK_NODE_PROGRAMMER_CLI}"
                    -c ${TRACK_NODE_FLASH_CONNECT_ARGS}
                    -w "$<TARGET_FILE:${TRACK_NODE_TARGET}>"
                    ${TRACK_NODE_FLASH_OPTIONS}
            DEPENDS ${TRACK_NODE_TARGET}
            USES_TERMINAL
            VERBATIM
        )
        add_custom_target(flash-under-reset
            COMMAND "${TRACK_NODE_PROGRAMMER_CLI}"
                    -c port=SWD mode=UR reset=HWrst freq=400
                    -w "$<TARGET_FILE:${TRACK_NODE_TARGET}>"
                    ${TRACK_NODE_FLASH_OPTIONS}
            DEPENDS ${TRACK_NODE_TARGET}
            USES_TERMINAL
            VERBATIM
        )
    else()
        message(STATUS "STM32_Programmer_CLI not found; flash target unavailable")
    endif()
else()
    enable_testing()
    add_subdirectory(test/host)
endif()
