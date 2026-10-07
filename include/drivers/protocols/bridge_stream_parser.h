#pragma once

#include "drivers/protocols/bridge_codec.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace ble_bridge
{
enum class stream_event_type
{
	none,
	frame,
	parse_error,
	frame_too_long
};

struct stream_event
{
	stream_event_type type = stream_event_type::none;
	ble_bridge::frame value{};
	codec_error error = codec_error::frame_too_short;
};

class bridge_stream_parser
{
public:
	explicit bridge_stream_parser(const bridge_codec &codec);

	stream_event feed(std::uint8_t byte);
	void reset();

private:
	const bridge_codec &codec_;
	std::array<std::uint8_t, wire_frame_capacity> buffer_{};
	std::size_t length_ = 0;
	bool discarding_ = false;
};
}
