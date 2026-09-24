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

    add_executable(track_node.elf)
    add_subdirectory(src)
    include(cmake/stm32cube_sources.cmake)
    include(cmake/vendor_sources.cmake)

    target_include_directories(track_node.elf PRIVATE
        include
        src/libs/freertos
        lib/FreeRTOS/include
        lib/FreeRTOS/portable/ARM_CM4F
        lib/fatfs/source
        lib/expected/include
        "${HAL_ROOT}/Inc"
        "${CMSIS_ROOT}/Include"
        "${DEVICE_ROOT}/Include"
    )

    target_compile_definitions(track_node.elf PRIVATE
        STM32F411xE USE_HAL_DRIVER HSE_VALUE=25000000U
    )
    target_compile_options(track_node.elf PRIVATE
        -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
        -ffunction-sections -fdata-sections
        $<$<CONFIG:Debug>:-O0;-g3;-ggdb3>
    )
    target_link_options(track_node.elf PRIVATE
        -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
        --specs=nano.specs --specs=nosys.specs
        "-T${CMAKE_CURRENT_SOURCE_DIR}/ld/STM32F411CEUX_FLASH.ld"
        -Wl,--gc-sections
        -Wl,-u,_printf_float
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/track_node.map"
    )

    add_custom_command(TARGET track_node.elf POST_BUILD
        COMMAND "${ARM_OBJCOPY}" -O binary "$<TARGET_FILE:track_node.elf>" "${CMAKE_CURRENT_BINARY_DIR}/track_node.bin"
        COMMAND "${ARM_SIZE}" "$<TARGET_FILE:track_node.elf>"
        VERBATIM
    )
else()
    enable_testing()
    add_subdirectory(test/host)
endif()
