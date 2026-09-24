cmake_minimum_required(VERSION 3.24)
project(TrackNode LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CROSSCOMPILING)
    set(STM32CUBE_F4_ROOT "" CACHE PATH "Path to STM32CubeF4 package root")
    if(NOT EXISTS "${STM32CUBE_F4_ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal.h")
        message(FATAL_ERROR "Set STM32CUBE_F4_ROOT to the STM32CubeF4 package root")
    endif()

    set(HAL_ROOT "${STM32CUBE_F4_ROOT}/Drivers/STM32F4xx_HAL_Driver")
    set(CMSIS_ROOT "${STM32CUBE_F4_ROOT}/Drivers/CMSIS")
    set(DEVICE_ROOT "${CMSIS_ROOT}/Device/ST/STM32F4xx")
    include(cmake/track_node_options.cmake)

    add_executable(${TRACK_NODE_TARGET})
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
else()
    enable_testing()
    add_subdirectory(test/host)
endif()
