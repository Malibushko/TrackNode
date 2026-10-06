#pragma once

#include <cstddef>
#include <cstdint>

namespace ble
{
struct data_view
{
	const std::uint8_t *data = nullptr;
	std::size_t size = 0;
};

struct mutable_data_view
{
	std::uint8_t *data = nullptr;
	std::size_t capacity = 0;
};
}
