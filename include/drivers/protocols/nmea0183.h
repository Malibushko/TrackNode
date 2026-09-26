#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "nmea0183_types.h"

enum class nmea_protocol_event_type
{
	none,
	sentence,
	parse_error,
	line_too_long
};

struct nmea_protocol_event
{
	nmea_protocol_event_type type = nmea_protocol_event_type::none;
	nmea_sentence value{};
	nmea_parse_error error = nmea_parse_error::invalid_syntax;
};

class nmea_protocol
{
public:
	static constexpr std::size_t max_line_length = 82;

	nmea_protocol_event feed(std::uint8_t byte);
	void reset();

private:
	std::array<char, max_line_length> line_{};
	std::size_t length_ = 0;
	bool discarding_ = false;
};