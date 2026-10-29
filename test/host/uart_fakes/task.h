#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void *;
inline void (*fake_wait_hook)() = nullptr;
inline unsigned fake_notifications = 0;
inline constexpr unsigned taskSCHEDULER_NOT_STARTED = 0;
inline unsigned xTaskGetSchedulerState() { return 1; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void *>(1); }
inline std::uint32_t ulTaskNotifyTake(BaseType_t, TickType_t ticks)
{
    if (ticks != 0 && fake_wait_hook) fake_wait_hook();
    return 0;
}
inline void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *) { ++fake_notifications; }
inline TickType_t fake_uart_ticks = 0;
inline TickType_t xTaskGetTickCount() { return fake_uart_ticks; }
inline TickType_t xTaskGetTickCountFromISR() { return fake_uart_ticks; }
