#pragma once
#include <cerrno>
#include "hal.h"

extern UART_HandleTypeDef UART_Handler;

extern "C" int _write(int fd, char* data, int len)
{
    if (fd != 1 && fd != 2) { // stdout и stderr
        errno = EBADF;
        return -1;
    }

    if (len < 0 || len > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (len == 0)
        return 0;

    auto status = HAL_UART_Transmit(
        &UART_Handler,
        reinterpret_cast<uint8_t*>(data),
        static_cast<uint16_t>(len),
        100);

    if (status != HAL_OK) {
        errno = EIO;
        return -1;
    }

    return len;
}
