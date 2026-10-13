#pragma once
#include <cassert>
inline int usb_critical_depth = 0;
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5U << 4)
#define taskENTER_CRITICAL() (++usb_critical_depth)
#define taskEXIT_CRITICAL() do { assert(usb_critical_depth > 0); --usb_critical_depth; } while (0)
