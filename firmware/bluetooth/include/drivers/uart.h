#pragma once

#include "common/buffer.h"
#include "core/result.h"
#include <cstdint>

namespace ble
{
class uart
{
public:
	virtual ~uart() = default;

	virtual status init() = 0;
	virtual void stop() = 0;

	virtual status write(data_view data, std::uint32_t timeout_ms) = 0;
	virtual result<std::size_t> read(mutable_data_view output, std::uint32_t timeout_ms) = 0;
};
}
