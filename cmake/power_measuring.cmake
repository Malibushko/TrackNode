set(TRACK_NODE_POWER_MEASURING_TARGET track_node_power_measuring.elf)
set(TRACK_NODE_POWER_MEASURING_BINARY "${PROJECT_BINARY_DIR}/track_node_power_measuring.bin")
set(TRACK_NODE_POWER_MEASURING_HAL_MODULES cortex flash flash_ex gpio pwr pwr_ex rcc rcc_ex)

add_executable(${TRACK_NODE_POWER_MEASURING_TARGET}
    tools/power_measuring/main.cpp
    "${HAL_ROOT}/Src/stm32f4xx_hal.c"
    "${DEVICE_ROOT}/Source/Templates/system_stm32f4xx.c"
    "${DEVICE_ROOT}/Source/Templates/gcc/startup_stm32f411xe.s"
)

foreach(module IN LISTS TRACK_NODE_POWER_MEASURING_HAL_MODULES)
    target_sources(${TRACK_NODE_POWER_MEASURING_TARGET} PRIVATE
        "${HAL_ROOT}/Src/stm32f4xx_hal_${module}.c"
    )
endforeach()

target_include_directories(${TRACK_NODE_POWER_MEASURING_TARGET} PRIVATE
    "${HAL_ROOT}/Inc"
    "${CMSIS_ROOT}/Include"
    "${DEVICE_ROOT}/Include"
)
target_compile_definitions(${TRACK_NODE_POWER_MEASURING_TARGET} PRIVATE ${TRACK_NODE_COMPILE_DEFINITIONS})
target_compile_options(${TRACK_NODE_POWER_MEASURING_TARGET} PRIVATE ${TRACK_NODE_COMPILE_OPTIONS})
target_link_options(${TRACK_NODE_POWER_MEASURING_TARGET} PRIVATE
    ${TRACK_NODE_ARCH_OPTIONS}
    --specs=nano.specs
    --specs=nosys.specs
    "-T${TRACK_NODE_LINKER_SCRIPT}"
    -Wl,--gc-sections
    "-Wl,-Map=${PROJECT_BINARY_DIR}/track_node_power_measuring.map"
)
add_custom_command(TARGET ${TRACK_NODE_POWER_MEASURING_TARGET} POST_BUILD
    COMMAND "${ARM_OBJCOPY}" -O binary "$<TARGET_FILE:${TRACK_NODE_POWER_MEASURING_TARGET}>" "${TRACK_NODE_POWER_MEASURING_BINARY}"
    COMMAND "${ARM_SIZE}" "$<TARGET_FILE:${TRACK_NODE_POWER_MEASURING_TARGET}>"
    VERBATIM
)

if(TRACK_NODE_PROGRAMMER_CLI)
    add_custom_target(flash-power-measuring
        COMMAND "${TRACK_NODE_PROGRAMMER_CLI}"
                -c port=SWD mode=UR reset=HWrst freq=400
                -w "$<TARGET_FILE:${TRACK_NODE_POWER_MEASURING_TARGET}>"
                ${TRACK_NODE_FLASH_OPTIONS}
        DEPENDS ${TRACK_NODE_POWER_MEASURING_TARGET}
        USES_TERMINAL
        VERBATIM
    )
endif()
