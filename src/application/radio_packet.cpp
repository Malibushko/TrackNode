#include "application/radio_packet.h"

#include <cmath>
#include <limits>
#include <span>

namespace
{
class packet_writer
{
public:
	explicit packet_writer(std::span<std::uint8_t> destination) : destination_(destination) {}

	void put_u8(std::uint8_t value)
	{
		if (offset_ == destination_.size())
		{
			overflow_ = true;
			return;
		}
		destination_[offset_++] = value;
	}

	void put_u16(std::uint16_t value)
	{
		put_u8(static_cast<std::uint8_t>(value));
		put_u8(static_cast<std::uint8_t>(value >> 8));
	}

	void put_u32(std::uint32_t value)
	{
		for (unsigned shift = 0; shift < 32; shift += 8)
			put_u8(static_cast<std::uint8_t>(value >> shift));
	}

	[[nodiscard]] bool ok() const { return !overflow_; }
	[[nodiscard]] std::size_t size() const { return offset_; }

private:
	std::span<std::uint8_t> destination_;
	std::size_t offset_ = 0;
	bool overflow_ = false;
};
}

//////////////////////////////////////////////////////////////////////////

result<radio_packet> format_radio_packet(const sensor_sample &sample,
	                                    const std::optional<gnss_sample> &gnss,
	                                    TickType_t recorded_at)
{
	if (!sample.valid || !std::isfinite(sample.temperature_c) ||
	    !std::isfinite(sample.pressure_pa) ||
	    sample.temperature_c < -327.68f || sample.temperature_c > 327.67f ||
	    sample.pressure_pa < 0 || static_cast<double>(sample.pressure_pa) >= 4294967296.0)
		return fail(component::service, error_code::invalid_argument);

	std::uint8_t flags = 0;
	std::int32_t latitude_e6 = 0;
	std::int32_t longitude_e6 = 0;
	std::uint16_t speed_cm_s = 0;
	std::uint32_t utc_unix_s = 0;
	std::uint16_t gnss_age_ms = 0;

	if (gnss)
	{
		const auto age_ms = pdTICKS_TO_MS(recorded_at - gnss->received_at);
		if (age_ms <= gnss_freshness_limit_ms)
		{
			gnss_age_ms = static_cast<std::uint16_t>(age_ms);
			if (gnss->position && std::isfinite(gnss->position->latitude) &&
			    std::isfinite(gnss->position->longitude) &&
			    gnss->position->latitude >= -90 && gnss->position->latitude <= 90 &&
			    gnss->position->longitude >= -180 && gnss->position->longitude <= 180)
			{
				flags |= 0x01;
				latitude_e6 = static_cast<std::int32_t>(std::llround(gnss->position->latitude * 1'000'000.0));
				longitude_e6 = static_cast<std::int32_t>(std::llround(gnss->position->longitude * 1'000'000.0));
			}

			if (gnss->speed_mps && std::isfinite(*gnss->speed_mps) &&
			    *gnss->speed_mps >= 0 && *gnss->speed_mps <= 655.35f)
			{
				flags |= 0x02;
				speed_cm_s = static_cast<std::uint16_t>(std::lround(*gnss->speed_mps * 100.0f));
			}

			if (gnss->utc_time && *gnss->utc_time >= 0 &&
			    static_cast<std::uint64_t>(*gnss->utc_time) <= std::numeric_limits<std::uint32_t>::max())
			{
				flags |= 0x04;
				utc_unix_s = static_cast<std::uint32_t>(*gnss->utc_time);
			}
		}
	}

	radio_packet packet{};
	packet_writer writer{packet.bytes};
	writer.put_u8('T');
	writer.put_u8('N');
	writer.put_u8(1); // wire version
	writer.put_u8(flags);
	writer.put_u32(sample.sequence);
	writer.put_u32(static_cast<std::uint32_t>(pdTICKS_TO_MS(sample.timestamp)));
	writer.put_u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(
	    std::lround(sample.temperature_c * 100.0f))));
	writer.put_u32(static_cast<std::uint32_t>(std::llround(sample.pressure_pa)));
	writer.put_u32(static_cast<std::uint32_t>(latitude_e6));
	writer.put_u32(static_cast<std::uint32_t>(longitude_e6));
	writer.put_u16(speed_cm_s);
	writer.put_u32(utc_unix_s);
	writer.put_u16(gnss_age_ms);

	if (!writer.ok())
		return fail(component::service, error_code::out_of_memory);
	packet.length = static_cast<std::uint8_t>(writer.size());
	return packet;
}
