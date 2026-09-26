#include "drivers/devices/neo6m.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>

namespace details
{
result<neo6m::snapshot> from_nmea_rmc_message(const nmea_sentence& sentence);
}

namespace common
{
std::uint32_t get_tick() { return 0; }
std::uint32_t ticks_to_ms(std::uint32_t ticks) { return ticks; }
}

namespace
{

result<neo6m::snapshot> decode(std::string_view body)
{
	unsigned checksum = 0;
	for (char byte : body)
	{
		checksum ^= static_cast<unsigned char>(byte);
	}
	char suffix[5]{};
	std::snprintf(suffix, sizeof(suffix), "*%02X", checksum);
	const std::string line = "$" + std::string(body) + suffix + "\n";

	nmea_protocol protocol;
	for (char byte : line)
	{
		const auto event = protocol.feed(static_cast<std::uint8_t>(byte));
		if (event.type == nmea_protocol_event_type::sentence)
		{
			return details::from_nmea_rmc_message(event.value);
		}
	}
	assert(false && "test input was not parsed as RMC");
	return fail(component::uart_device, error_code::invalid_argument);
}

//////////////////////////////////////////////////////////////////////////

result<neo6m::snapshot> decode_fields(std::string_view time, std::string_view status,
	std::string_view latitude, std::string_view lat_hemisphere, std::string_view longitude,
	std::string_view lon_hemisphere, std::string_view speed, std::string_view date)
{
	return decode("GPRMC," + std::string(time) + "," + std::string(status) + "," +
		std::string(latitude) + "," + std::string(lat_hemisphere) + "," + std::string(longitude) + "," +
		std::string(lon_hemisphere) + "," + std::string(speed) + ",," + std::string(date) + ",,,A");
}

//////////////////////////////////////////////////////////////////////////

}

int main()
{
	uart empty_transport;
	neo6m empty_device{ empty_transport };
	assert(empty_device.init());
	const auto empty_read = empty_device.read_snapshot(100);
	assert(!empty_read && empty_read.error().code == error_code::not_ready);

	const auto valid = decode_fields("083559.00", "A", "4717.11437", "N", "00833.91522", "E", "0.004", "091202");
	assert(valid && valid->position_valid && valid->speed_valid && valid->time_valid);
	assert(valid->latitude > 47.28 && valid->latitude < 47.29);
	assert(valid->longitude > 8.56 && valid->longitude < 8.57);
	assert(valid->utc_time == static_cast<std::time_t>(1039422959));
	assert(valid->received_at == 123);

	const auto empty_speed = decode_fields("083559", "A", "4717.11437", "S", "00833.91522", "W", "", "091202");
	assert(empty_speed && empty_speed->position_valid && !empty_speed->speed_valid && empty_speed->time_valid);
	assert(empty_speed->latitude < 0 && empty_speed->longitude < 0);

	const auto no_fix = decode_fields("083559", "V", "", "", "", "", "", "091202");
	assert(no_fix && !no_fix->position_valid && no_fix->time_valid);

	assert(!decode_fields("083559", "A", "4760.0000", "N", "00833.0", "E", "1.0", "091202"));
	assert(!decode_fields("083559", "A", "9000.0001", "N", "00833.0", "E", "1.0", "091202"));
	assert(!decode_fields("083559", "A", "4717.0", "X", "00833.0", "E", "1.0", "091202"));
	assert(!decode_fields("083559", "A", "4717.0", "N", "18100.0", "E", "1.0", "091202"));
	assert(!decode_fields("083559", "Q", "", "", "", "", "", "091202"));
	assert(!decode_fields("083559", "A", "4717.0", "N", "00833.0", "E", "-1", "091202"));

	assert(!decode_fields("246000", "A", "4717.0", "N", "00833.0", "E", "1.0", "091202")->time_valid);
	assert(!decode_fields("083559.x", "A", "4717.0", "N", "00833.0", "E", "1.0", "091202")->time_valid);
	assert(!decode_fields("083559", "A", "4717.0", "N", "00833.0", "E", "1.0", "321202")->time_valid);
	assert(!decode_fields("083559", "A", "4717.0", "N", "00833.0", "E", "1.0", "290223")->time_valid);
	assert(decode_fields("083559", "A", "4717.0", "N", "00833.0", "E", "1.0", "290224")->time_valid);

	std::puts("PASS: NEO-6M RMC ranges, date/time validity and optional speed");
}
