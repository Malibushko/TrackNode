#pragma once

#include <cstdint>

using BaseType_t = std::int32_t;
using TickType_t = std::uint32_t;

constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr BaseType_t errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY = -1;

#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#define pdTICKS_TO_MS(ticks) (static_cast<TickType_t>(ticks))
