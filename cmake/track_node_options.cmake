set(TRACK_NODE_TARGET track_node.elf)
set(TRACK_NODE_DEVICE STM32F411xE)
set(TRACK_NODE_HSE_VALUE 25000000U)

set(TRACK_NODE_LINKER_SCRIPT "${PROJECT_SOURCE_DIR}/ld/STM32F411CEUX_FLASH.ld")
set(TRACK_NODE_BINARY "${PROJECT_BINARY_DIR}/track_node.bin")
set(TRACK_NODE_MAP_FILE "${PROJECT_BINARY_DIR}/track_node.map")
set(TRACK_NODE_FLASH_CONNECT_ARGS port=SWD)
set(TRACK_NODE_FLASH_OPTIONS -v -rst)

set(TRACK_NODE_INCLUDE_DIRECTORIES
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src/libs/freertos"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/include"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/portable/ARM_CM4F"
    "${PROJECT_SOURCE_DIR}/lib/fatfs/source"
    "${PROJECT_SOURCE_DIR}/lib/expected/include"
    "${HAL_ROOT}/Inc"
    "${CMSIS_ROOT}/Include"
    "${DEVICE_ROOT}/Include"
)

set(TRACK_NODE_COMPILE_DEFINITIONS
    ${TRACK_NODE_DEVICE}
    USE_HAL_DRIVER
    HSE_VALUE=${TRACK_NODE_HSE_VALUE}
    $<$<CONFIG:Debug>:TRACK_NODE_DEBUG>
)

set(TRACK_NODE_ARCH_OPTIONS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
)

set(TRACK_NODE_COMPILE_OPTIONS
    ${TRACK_NODE_ARCH_OPTIONS}
    -ffunction-sections
    -fdata-sections
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-ggdb3>
)

set(TRACK_NODE_LINK_OPTIONS
    ${TRACK_NODE_ARCH_OPTIONS}
    --specs=nano.specs
    --specs=nosys.specs
    "-T${TRACK_NODE_LINKER_SCRIPT}"
    -Wl,--gc-sections
    -Wl,-u,_printf_float
    "-Wl,-Map=${TRACK_NODE_MAP_FILE}"
)
