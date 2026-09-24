#pragma once

#include "application/command.h"

#include <string_view>
#include <tl/expected.hpp>

enum class command_parse_error
{
	unknown_command,
	invalid_syntax,
	invalid_number
};

tl::expected<command, command_parse_error> parse_command(std::string_view line);
