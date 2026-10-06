#pragma once

#include "common/buffer.h"
#include "core/result.h"
#include "drivers/protocols/ble_bridge.h"

namespace ble
{
class bridge_codec
{
public:
	result<std::size_t> encode(const ble_bridge::frame &frame, mutable_data_view output) const;
	result<ble_bridge::frame> decode(data_view input) const;
};
}
