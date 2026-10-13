set(USB_DEVICE_ROOT "${STM32CUBE_F4_ROOT}/Middlewares/ST/STM32_USB_Device_Library")
if(NOT EXISTS "${USB_DEVICE_ROOT}/Class/CDC/Inc/usbd_cdc.h")
    message(FATAL_ERROR "STM32CubeF4 must include the USB Device Library with CDC")
endif()

target_include_directories(${TRACK_NODE_TARGET} PRIVATE
    "${USB_DEVICE_ROOT}/Core/Inc"
    "${USB_DEVICE_ROOT}/Class/CDC/Inc"
)
target_sources(${TRACK_NODE_TARGET} PRIVATE
    "${USB_DEVICE_ROOT}/Core/Src/usbd_core.c"
    "${USB_DEVICE_ROOT}/Core/Src/usbd_ctlreq.c"
    "${USB_DEVICE_ROOT}/Core/Src/usbd_ioreq.c"
    "${USB_DEVICE_ROOT}/Class/CDC/Src/usbd_cdc.c"
    "${HAL_ROOT}/Src/stm32f4xx_ll_usb.c"
)
