#pragma once

#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class uart
{
public:
	result<std::size_t> read(std::uint8_t*, std::size_t, std::uint32_t) { return std::size_t{ 0 }; }
	status write(const std::uint8_t* data, std::size_t size, std::uint32_t)
	{
		last_write.assign(data, data + size);
		return {};
	}
	std::vector<std::uint8_t> last_write;
};

inline TickType_t xTaskGetTickCount() { return 123; }
