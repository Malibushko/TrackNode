#pragma once
#include <cassert>
using TaskHandle_t = void *;
inline unsigned usb_notifications = 0;
inline unsigned usb_isr_notifications = 0;
inline void (*usb_wait_hook)() = nullptr;
#define pdTRUE 1
#define pdFALSE 0
#define portYIELD_FROM_ISR(value) ((void)(value))
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void *>(1); }
inline unsigned ulTaskNotifyTake(int clear, unsigned ticks)
{
    if (ticks && usb_wait_hook) usb_wait_hook();
    const auto value = usb_notifications;
    if (clear) usb_notifications = 0;
    return value;
}
inline void xTaskNotifyGive(TaskHandle_t) { ++usb_notifications; }
inline void vTaskNotifyGiveFromISR(TaskHandle_t, int *) { ++usb_notifications; ++usb_isr_notifications; }
inline int usb_critical_depth = 0;
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5U << 4)
#define taskENTER_CRITICAL() (++usb_critical_depth)
#define taskEXIT_CRITICAL() do { assert(usb_critical_depth > 0); --usb_critical_depth; } while (0)
inline unsigned usb_event_notifications = 0;
inline void xTaskNotifyGiveIndexed(TaskHandle_t, unsigned) { ++usb_event_notifications; }
inline void vTaskNotifyGiveIndexedFromISR(TaskHandle_t, unsigned, int *) { ++usb_event_notifications; }
