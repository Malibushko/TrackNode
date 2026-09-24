#pragma once

#include "application/command.h"
#include "infrastructure/command_parser.h"

#include <array>
#include <cstddef>
#include <cstdint>

enum class protocol_event_type
{
	none,
	command,
	parse_error,
	line_too_long
};

struct protocol_event
{
	protocol_event_type type = protocol_event_type::none;
	::command value{};
	command_parse_error error = command_parse_error::invalid_syntax;
};

// Collects one bounded UART line, then hands its contents to the pure parser.
class track_node_control_protocol
{
public:
	static constexpr std::size_t max_line_length = 64;

	protocol_event feed(std::uint8_t byte);
	void reset();

private:
	std::array<char, max_line_length> line_{};
	std::size_t length_ = 0;
	bool discarding_ = false;
};
