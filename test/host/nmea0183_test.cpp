#include "drivers/protocols/nmea0183.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{

nmea_protocol_event feed_line(nmea_protocol& protocol, std::string_view line)
{
	nmea_protocol_event result{};
	for (char byte : line)
	{
		const auto event = protocol.feed(static_cast<std::uint8_t>(byte));
		if (event.type != nmea_protocol_event_type::none)
		{
			result = event;
		}
	}
	return result;
}

//////////////////////////////////////////////////////////////////////////

std::string with_checksum(std::string_view payload)
{
	unsigned checksum = 0;
	for (char byte : payload)
	{
		checksum ^= static_cast<unsigned char>(byte);
	}
	char suffix[5]{};
	std::snprintf(suffix, sizeof(suffix), "*%02X", checksum);
	return "$" + std::string(payload) + suffix + "\r\n";
}

//////////////////////////////////////////////////////////////////////////

}

int main()
{
	nmea_protocol protocol;
	const auto valid = feed_line(protocol,
		"$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*57\r\n");
	assert(valid.type == nmea_protocol_event_type::sentence);
	assert(valid.value.type == nmea_sentence_type::rmc);
	assert(valid.value.talker.first == 'G' && valid.value.talker.second == 'P');
	assert(valid.value.rmc.utc_time.view() == "083559.00");
	assert(valid.value.rmc.status == 'A');
	assert(valid.value.rmc.latitude.view() == "4717.11437");
	assert(valid.value.rmc.lat_hemisphere == 'N');
	assert(valid.value.rmc.longitude.view() == "00833.91522");
	assert(valid.value.rmc.lon_hemisphere == 'E');
	assert(valid.value.rmc.speed_knots.view() == "0.004");
	assert(valid.value.rmc.date.view() == "091202");
	assert(valid.value.rmc.mag_variation.view().empty());
	assert(valid.value.rmc.mode_indicator == 'A');

	const auto invalid_fix = feed_line(protocol, with_checksum("GPRMC,083600.00,V,,,,,,,091202,,,N"));
	assert(invalid_fix.type == nmea_protocol_event_type::sentence);
	assert(invalid_fix.value.rmc.status == 'V');
	assert(invalid_fix.value.rmc.latitude.view().empty());
	assert(invalid_fix.value.rmc.longitude.view().empty());

	const auto corrupt = feed_line(protocol,
		"$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*00\n");
	assert(corrupt.type == nmea_protocol_event_type::parse_error);
	assert(corrupt.error == nmea_parse_error::checksum_mismatch);

	const auto missing_checksum = feed_line(protocol, "$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A\n");
	assert(missing_checksum.type == nmea_protocol_event_type::parse_error);
	assert(missing_checksum.error == nmea_parse_error::invalid_syntax);

	const auto bad_hex = feed_line(protocol, "$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*ZZ\n");
	assert(bad_hex.type == nmea_protocol_event_type::parse_error);
	assert(bad_hex.error == nmea_parse_error::invalid_syntax);

	const auto unsupported = feed_line(protocol,
		"$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,8,1.01,499.6,M,48.0,M,,0*5B\n");
	assert(unsupported.type == nmea_protocol_event_type::parse_error);
	assert(unsupported.error == nmea_parse_error::unsupported_sentence);

	const auto missing_field = feed_line(protocol, with_checksum("GPRMC,083559.00,A,4717.11437,N"));
	assert(missing_field.type == nmea_protocol_event_type::parse_error);
	assert(missing_field.error == nmea_parse_error::field_count_mismatch);

	assert(feed_line(protocol, " \t \r\n").type == nmea_protocol_event_type::none);
	std::string overflow(nmea_protocol::max_line_length + 1, 'X');
	assert(feed_line(protocol, overflow).type == nmea_protocol_event_type::none);
	assert(protocol.feed('\n').type == nmea_protocol_event_type::line_too_long);
	assert(feed_line(protocol, "$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*57\n")
		.type == nmea_protocol_event_type::sentence);

	assert(feed_line(protocol, "$GPRMC,083559.00,A").type == nmea_protocol_event_type::none);
	protocol.reset();
	assert(feed_line(protocol, "$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*57\n")
		.type == nmea_protocol_event_type::sentence);

	std::puts("PASS: NMEA RMC parsing, checksums, invalid fix, framing and overflow recovery");
}
