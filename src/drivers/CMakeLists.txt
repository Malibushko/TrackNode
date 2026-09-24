target_sources(track_node.elf PRIVATE
    adc.cpp
    gpio.cpp
    i2c_bus.cpp
    i2c_device.cpp
    pwm.cpp
    spi_bus.cpp
    spi_device.cpp
    timer.cpp
    uart.cpp
    watchdog.cpp
)

add_subdirectory(devices)
