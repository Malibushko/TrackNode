#include "application/radio_packet.h"

#include <cassert>
#include <cstdint>
#include <limits>

namespace
{
std::uint16_t get_u16(const radio_packet &packet, std::size_t offset)
{
	return static_cast<std::uint16_t>(packet.bytes[offset]) |
	       static_cast<std::uint16_t>(packet.bytes[offset + 1]) << 8;
}

std::uint32_t get_u32(const radio_packet &packet, std::size_t offset)
{
	return static_cast<std::uint32_t>(packet.bytes[offset]) |
	       static_cast<std::uint32_t>(packet.bytes[offset + 1]) << 8 |
	       static_cast<std::uint32_t>(packet.bytes[offset + 2]) << 16 |
	       static_cast<std::uint32_t>(packet.bytes[offset + 3]) << 24;
}
}

int main()
{
	sensor_sample sample{ .timestamp = 1234, .sequence = 7,
	                      .temperature_c = -12.34f, .pressure_pa = 100000.0f,
	                      .valid = true };
	auto packet = format_radio_packet(sample, std::nullopt, sample.timestamp);
	assert(packet);
	assert(packet->length == 34);
	assert(packet->bytes[0] == 'T' && packet->bytes[1] == 'N');
	assert(packet->bytes[2] == 1 && packet->bytes[3] == 0);
	assert(get_u32(*packet, 4) == 7);
	assert(get_u32(*packet, 8) == 1234);
	assert(static_cast<std::int16_t>(get_u16(*packet, 12)) == -1234);
	assert(get_u32(*packet, 14) == 100000);
	assert(get_u32(*packet, 18) == 0 && get_u16(*packet, 32) == 0);

	gnss_sample gnss{};
	gnss.received_at = 1200;
	gnss.position = geo_position{46.4542, 30.7575};
	gnss.speed_mps = 1.25f;
	gnss.utc_time = 1234567890;
	packet = format_radio_packet(sample, gnss, sample.timestamp);
	assert(packet && packet->bytes[3] == 0x07);
	assert(static_cast<std::int32_t>(get_u32(*packet, 18)) == 46454200);
	assert(static_cast<std::int32_t>(get_u32(*packet, 22)) == 30757500);
	assert(get_u16(*packet, 26) == 125);
	assert(get_u32(*packet, 28) == 1234567890);
	assert(get_u16(*packet, 32) == 34);

	packet = format_radio_packet(sample, gnss, sample.timestamp + 6000);
	assert(packet && packet->bytes[3] == 0);
	assert(get_u32(*packet, 18) == 0 && get_u16(*packet, 32) == 0);

	sample.valid = false;
	assert(!format_radio_packet(sample, std::nullopt, sample.timestamp));
	sample.valid = true;
	sample.temperature_c = std::numeric_limits<float>::infinity();
	assert(!format_radio_packet(sample, std::nullopt, sample.timestamp));
}
