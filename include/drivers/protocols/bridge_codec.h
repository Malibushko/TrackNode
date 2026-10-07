#pragma once

#include "drivers/protocols/ble_bridge.h"
#include <span>
#include <tl/expected.hpp>

namespace ble_bridge
{
enum class codec_error
{
	payload_too_large,
	output_too_small,
	frame_too_large,
	missing_delimiter,
	invalid_cobs,
	frame_too_short,
	checksum_mismatch,
	unsupported_version,
	length_mismatch
};

class bridge_codec
{
public:
	tl::expected<std::size_t, codec_error> encode(const frame &message, std::span<std::uint8_t> output) const;
	tl::expected<frame, codec_error> decode(std::span<const std::uint8_t> input) const;
};
}
