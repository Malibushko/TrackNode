#pragma once

#include "common/buffer.h"
#include "core/result.h"
#include "protocols/bridge_frame.h"

namespace ble
{
class bridge_codec
{
public:
	result<std::size_t> encode(const bridge_frame &frame, mutable_data_view output) const;
	result<bridge_frame> decode(data_view input) const;
};
}
