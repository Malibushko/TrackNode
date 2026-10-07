#include "drivers/protocols/bridge_stream_parser.h"

namespace ble_bridge
{
bridge_stream_parser::bridge_stream_parser(const bridge_codec &codec) : codec_(codec) {}

stream_event bridge_stream_parser::feed(std::uint8_t byte)
{
	if (discarding_)
	{
		if (byte == 0)
		{
			reset();
		}

		return {};
	}

	if (byte == 0 && length_ == 0)
	{
		return {};
	}

	if (length_ == buffer_.size())
	{
		length_ = 0;
		discarding_ = byte != 0;
		return stream_event{
			.type = stream_event_type::frame_too_long,
			.error = codec_error::frame_too_large
		};
	}

	buffer_[length_++] = byte;
	if (byte != 0)
	{
		return {};
	}

	const auto decoded = codec_.decode(std::span{buffer_.data(), length_});
	length_ = 0;
	if (!decoded)
	{
		return stream_event{
			.type = stream_event_type::parse_error,
			.error = decoded.error()
		};
	}

	return stream_event{
		.type = stream_event_type::frame,
		.value = *decoded
	};
}

void bridge_stream_parser::reset()
{
	length_ = 0;
	discarding_ = false;
}
}
