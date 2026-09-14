#pragma once

#include "common/command_protocol.h"

enum class command
{
	blink_once,
	blink_twice,
	blink_thrice,
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
		switch (state_)
		{
			case state::waiting_header:
			{
				if (byte == 'b')
					state_ = state::waiting_command;

				break;
			}

			case state::waiting_command:
			{
				state_ = state::waiting_header;

				switch (byte)
				{
					case '0':
						return command::blink_once;

					case '1':
						return command::blink_twice;

					case '2':
						return command::blink_thrice;

					default:
						break;
				}

				break;
			}
		}

		return std::nullopt;
	}

private:
	enum class state
	{
		waiting_header,
		waiting_command,
	};

	state state_ = state::waiting_header;
};