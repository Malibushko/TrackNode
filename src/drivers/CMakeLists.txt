target_sources(${TRACK_NODE_TARGET} PRIVATE
    protocols/bridge_codec.cpp
    protocols/bridge_stream_parser.cpp
    adc.cpp
    gpio.cpp
    gpio_interrupt.cpp
    i2c_bus.cpp
    i2c_device.cpp
    pwm.cpp
    spi_bus.cpp
    spi_device.cpp
    timer.cpp
    uart.cpp
    serial_device.cpp
    uart_serial_device.cpp
    usb_cdc_serial_device.cpp
    usb_device.cpp
    usb_cdc.cpp
    usb_stm32.cpp
    watchdog.cpp
)

add_subdirectory(devices)
