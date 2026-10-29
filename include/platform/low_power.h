#pragma once

#include "drivers/gpio.h"
#include "FreeRTOS.h"

#include <cstdint>

#ifndef TRACK_NODE_STOP_MODE
#define TRACK_NODE_STOP_MODE 0
#endif

namespace low_power
{
using stop_veto = bool (*)(const void *context);

inline constexpr bool enabled = TRACK_NODE_STOP_MODE && configUSE_TICKLESS_IDLE;

status start();
status add_stop_veto(stop_veto veto, const void *context);
status add_wake_pin(pin input);
[[nodiscard]] bool stop_available();
}

extern "C" void track_node_suppress_ticks_and_sleep(TickType_t expected_idle_ticks);
