#include "infrastructure/command_parser.h"

#include <charconv>

namespace
{
bool is_separator(char value)
{
	return value == ' ' || value == '\t';
}

//////////////////////////////////////////////////////////////////////////

std::string_view next_token(std::string_view &remaining)
{
	while (!remaining.empty() && is_separator(remaining.front()))
	{
		remaining.remove_prefix(1);
	}

	const auto end = remaining.find_first_of(" \t");
	if (end == std::string_view::npos)
	{
		const auto token = remaining;
		remaining = {};
		return token;
	}

	const auto token = remaining.substr(0, end);
	remaining.remove_prefix(end);
	return token;
}

//////////////////////////////////////////////////////////////////////////

bool matches(std::string_view token, std::string_view expected)
{
	if (token.size() != expected.size())
	{
		return false;
	}
	for (std::size_t index = 0; index < token.size(); ++index)
	{
		char value = token[index];
		if (value >= 'a' && value <= 'z')
		{
			value -= 'a' - 'A';
		}
		if (value != expected[index])
		{
			return false;
		}
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
}

//////////////////////////////////////////////////////////////////////////

tl::expected<command, command_parse_error> parse_command(std::string_view line)
{
	const auto verb = next_token(line);
	const auto noun = next_token(line);
	if (matches(verb, "BOOTLOAD"))
	{
		if (!noun.empty())
		{
			return tl::make_unexpected(command_parse_error::invalid_syntax);
		}
		return command{ command_type::bootload };
	}

	if (matches(verb, "PING"))
	{
		if (!noun.empty())
		{
			return tl::make_unexpected(command_parse_error::invalid_syntax);
		}
		return command{ command_type::ping };
	}

	if (matches(verb, "GET"))
	{
		if (!next_token(line).empty())
		{
			return tl::make_unexpected(command_parse_error::invalid_syntax);
		}
		if (matches(noun, "TEMP"))
		{
			return command{ command_type::read_temperature };
		}
		if (matches(noun, "PRESSURE"))
		{
			return command{ command_type::read_pressure };
		}
		if (matches(noun, "GNSS"))
		{
			return command{ command_type::read_gnss };
		}
		if (matches(noun, "LOG"))
		{
			return command{ command_type::read_log };
		}
		if (matches(noun, "VOLTAGE"))
		{
			return command{ command_type::read_voltage };
		}
		if (matches(noun, "BATTERY"))
		{
			return command{ command_type::read_battery };
		}
		return tl::make_unexpected(command_parse_error::unknown_command);
	}

	if (matches(verb, "SET"))
	{
		if (matches(noun, "SAMPLE_PERIOD"))
		{
			const auto argument = next_token(line);
			if (argument.empty() || !next_token(line).empty())
			{
				return tl::make_unexpected(command_parse_error::invalid_syntax);
			}
			std::uint32_t period_ms = 0;
			const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), period_ms);
			if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size())
			{
				return tl::make_unexpected(command_parse_error::invalid_number);
			}

			return command{ command_type::set_sample_period, period_ms };
		}

		if (matches(noun, "POWER_MODE"))
		{
			const auto argument = next_token(line);
			if (argument == "LOW")
			{
				return command{ command_type::set_power_mode, {}, power_mode::low };
			}
			if (argument == "HIGH")
			{
				return command{ command_type::set_power_mode, {}, power_mode::high };
			}

			return tl::make_unexpected(command_parse_error::invalid_syntax);
		}

		return tl::make_unexpected(command_parse_error::unknown_command);
	}

	if (matches(verb, "RESET"))
	{
		if (!matches(noun, "STORAGE"))
		{
			return tl::make_unexpected(command_parse_error::unknown_command);
		}
		const auto confirmation = next_token(line);
		if (!next_token(line).empty())
		{
			return tl::make_unexpected(command_parse_error::invalid_syntax);
		}
		if (confirmation.empty())
		{
			return command{ command_type::reset_storage_prepare };
		}
		if (matches(confirmation, "CONFIRM"))
		{
			return command{ command_type::reset_storage_confirm };
		}
		return tl::make_unexpected(command_parse_error::invalid_syntax);
	}

	return tl::make_unexpected(command_parse_error::unknown_command);
}

//////////////////////////////////////////////////////////////////////////
