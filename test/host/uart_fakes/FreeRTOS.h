#pragma once
#include <cstddef>
#include <cstdint>
using BaseType_t = std::int32_t;
using TickType_t = std::uint32_t;
constexpr BaseType_t pdPASS = 1, pdFAIL = 0, pdTRUE = 1, pdFALSE = 0, errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY = -1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#define portYIELD_FROM_ISR(woken) ((void)(woken))
