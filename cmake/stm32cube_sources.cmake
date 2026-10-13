set(HAL_MODULES
    adc adc_ex cortex dma dma_ex flash flash_ex gpio i2c i2c_ex iwdg
    pcd pcd_ex pwr pwr_ex rcc rcc_ex spi tim tim_ex uart
)

target_sources(${TRACK_NODE_TARGET} PRIVATE
    "${HAL_ROOT}/Src/stm32f4xx_hal.c"
    "${DEVICE_ROOT}/Source/Templates/system_stm32f4xx.c"
    "${DEVICE_ROOT}/Source/Templates/gcc/startup_stm32f411xe.s"
)

foreach(module IN LISTS HAL_MODULES)
    target_sources(${TRACK_NODE_TARGET} PRIVATE
        "${HAL_ROOT}/Src/stm32f4xx_hal_${module}.c"
    )
endforeach()
