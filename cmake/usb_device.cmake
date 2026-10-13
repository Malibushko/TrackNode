set(USB_DEVICE_ROOT "${STM32CUBE_F4_ROOT}/Middlewares/ST/STM32_USB_Device_Library")
set(TRACK_NODE_USB_VID "0x0483" CACHE STRING "Bench USB vendor ID")
set(TRACK_NODE_USB_PID "0x5740" CACHE STRING "Bench USB product ID")
target_compile_definitions(${TRACK_NODE_TARGET} PRIVATE
    TRACK_NODE_USB_VID=${TRACK_NODE_USB_VID}
    TRACK_NODE_USB_PID=${TRACK_NODE_USB_PID}
)
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
