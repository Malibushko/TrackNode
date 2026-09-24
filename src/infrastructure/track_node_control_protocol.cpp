#include "infrastructure/track_node_control_protocol.h"

protocol_event track_node_control_protocol::feed(std::uint8_t byte)
{
	if (byte != '\r' && byte != '\n')
	{
		if (!discarding_)
		{
			if (length_ == line_.size())
			{
				discarding_ = true;
			}
			else
			{
				line_[length_++] = static_cast<char>(byte);
			}
		}
		return {};
	}

	if (discarding_)
	{
		discarding_ = false;
		length_ = 0;
		return { protocol_event_type::line_too_long };
	}
	bool blank = true;
	for (std::size_t index = 0; index < length_; ++index)
	{
		if (line_[index] != ' ' && line_[index] != '\t')
		{
			blank = false;
			break;
		}
	}
	if (blank)
	{
		length_ = 0;
		return {};
	}

	const auto parsed = parse_command({ line_.data(), length_ });
	length_ = 0;
	if (!parsed)
	{
		return { protocol_event_type::parse_error, {}, parsed.error() };
	}
	return { protocol_event_type::command, *parsed };
}

//////////////////////////////////////////////////////////////////////////

void track_node_control_protocol::reset()
{
	length_ = 0;
	discarding_ = false;
}

//////////////////////////////////////////////////////////////////////////
