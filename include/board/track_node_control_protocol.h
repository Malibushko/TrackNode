#pragma once

#include "common/command_protocol.h"

enum class command
{
	ping,
	read_temperature,
	read_pressure,
};

template<ByteInput Input>
class track_node_control_protocol : public command_protocol<Input, command, track_node_control_protocol<Input>>
{
	using base = command_protocol<
		Input,
		command,
		track_node_control_protocol<Input>
	>;

public:
	explicit track_node_control_protocol(Input& input)
		: base(input)
	{}

	std::optional<command> decode(std::uint8_t byte)
	{
		if (byte == 'p')
		{
			return command::ping;
		}

		if (byte == 't')
		{
			return command::read_temperature;
		}

		if (byte == 'c')
		{
			return command::read_pressure;
		}

		return std::nullopt;
	}
};