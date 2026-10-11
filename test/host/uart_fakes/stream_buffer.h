#pragma once
#include "FreeRTOS.h"
using StreamBufferHandle_t = void *;
inline void (*fake_receive_hook)() = nullptr;
inline StreamBufferHandle_t xStreamBufferCreate(std::size_t, std::size_t) { return reinterpret_cast<void *>(2); }
inline std::size_t xStreamBufferReceive(StreamBufferHandle_t, void *, std::size_t, TickType_t)
{
    if (fake_receive_hook) fake_receive_hook();
    return 0;
}
inline std::size_t xStreamBufferSendFromISR(StreamBufferHandle_t, const void *, std::size_t size, BaseType_t *) { return size; }
inline std::size_t xStreamBufferBytesAvailable(StreamBufferHandle_t) { return 0; }
