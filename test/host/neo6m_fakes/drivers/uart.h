#pragma once

#include "core/result.h"

#include <cstddef>
#include <cstdint>

class uart
{
public:
	result<std::size_t> read(std::uint8_t*, std::size_t, std::uint32_t) { return std::size_t{ 0 }; }
};

inline TickType_t xTaskGetTickCount() { return 123; }
