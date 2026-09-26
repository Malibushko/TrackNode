#include "infrastructure/command_parser.h"
#include "infrastructure/track_node_control_protocol.h"

#include <cassert>
#include <cstdio>
#include <string>

int main()
{
	auto ping = parse_command("  ping  ");
	assert(ping && ping->type == command_type::ping);
	assert(parse_command("GET TEMP")->type == command_type::read_temperature);
	assert(parse_command("get pressure")->type == command_type::read_pressure);
	assert(parse_command("get gnss")->type == command_type::read_gnss);
	assert(parse_command("GET\tLOG")->type == command_type::read_log);
	auto period = parse_command("SET SAMPLE_PERIOD 5000");
	assert(period && period->type == command_type::set_sample_period);
	assert(period->sample_period_ms == 5000);
	assert(parse_command("SET SAMPLE_PERIOD 4294967295")->sample_period_ms == UINT32_MAX);
	assert(parse_command("SET SAMPLE_PERIOD 4294967296").error() == command_parse_error::invalid_number);
	assert(parse_command("SET SAMPLE_PERIOD -1").error() == command_parse_error::invalid_number);
	assert(parse_command("SET SAMPLE_PERIOD 12x").error() == command_parse_error::invalid_number);
	assert(parse_command("SET SAMPLE_PERIOD").error() == command_parse_error::invalid_syntax);
	assert(parse_command("PING extra").error() == command_parse_error::invalid_syntax);
	assert(parse_command("GET TEMP extra").error() == command_parse_error::invalid_syntax);
	assert(parse_command("GET GNSS extra").error() == command_parse_error::invalid_syntax);
	assert(parse_command("GET UNKNOWN").error() == command_parse_error::unknown_command);
	assert(parse_command("WHAT").error() == command_parse_error::unknown_command);
	assert(parse_command("RESET STORAGE")->type == command_type::reset_storage_prepare);
	assert(parse_command("reset storage confirm")->type == command_type::reset_storage_confirm);
	assert(parse_command("RESET STORAGE extra").error() == command_parse_error::invalid_syntax);
	assert(parse_command("RESET STORAGE CONFIRM extra").error() == command_parse_error::invalid_syntax);
	track_node_control_protocol format_protocol;
	for (char byte : std::string("RESET STORAGE CONFIRM\r"))
	{
		const auto event = format_protocol.feed(static_cast<std::uint8_t>(byte));
		if (byte == '\r')
		{
			assert(event.type == protocol_event_type::command);
			assert(event.value.type == command_type::reset_storage_confirm);
		}
	}

	track_node_control_protocol protocol;
	for (char byte : std::string("GET TEMP\r\n"))
	{
		const auto event = protocol.feed(static_cast<std::uint8_t>(byte));
		if (byte == '\r')
		{
			assert(event.type == protocol_event_type::command);
			assert(event.value.type == command_type::read_temperature);
		}
		else
		{
			assert(event.type == protocol_event_type::none);
		}
	}
	assert(protocol.feed('\n').type == protocol_event_type::none);
	for (char byte : std::string("  \t \n"))
	{
		assert(protocol.feed(static_cast<std::uint8_t>(byte)).type == protocol_event_type::none);
	}
	for (char byte : std::string("GET LOG\r"))
	{
		const auto event = protocol.feed(static_cast<std::uint8_t>(byte));
		if (byte == '\r')
		{
			assert(event.type == protocol_event_type::command);
			assert(event.value.type == command_type::read_log);
		}
	}
	for (char byte : std::string("SET SAMPLE_PERIOD abc\n"))
	{
		const auto event = protocol.feed(static_cast<std::uint8_t>(byte));
		if (byte == '\n')
		{
			assert(event.type == protocol_event_type::parse_error);
			assert(event.error == command_parse_error::invalid_number);
		}
	}
	for (std::size_t index = 0; index < track_node_control_protocol::max_line_length + 1; ++index)
	{
		assert(protocol.feed('A').type == protocol_event_type::none);
	}
	assert(protocol.feed('\n').type == protocol_event_type::line_too_long);
	for (char byte : std::string("GET TEMP"))
	{
		assert(protocol.feed(static_cast<std::uint8_t>(byte)).type == protocol_event_type::none);
	}
	protocol.reset();
	for (char byte : std::string("PING\n"))
	{
		const auto event = protocol.feed(static_cast<std::uint8_t>(byte));
		if (byte == '\n')
		{
			assert(event.type == protocol_event_type::command);
			assert(event.value.type == command_type::ping);
		}
	}
	std::puts("PASS: command syntax, arguments, line framing and overflow recovery");
}
