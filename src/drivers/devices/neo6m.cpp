#include "drivers/devices/neo6m.h"

#include "common/common.h"

namespace
{
bool parse_fixed_point(std::string_view text, double& out)
{
    if (text.empty())
    {
        return false;
    }

    std::size_t index = 0;
    bool negative = false;
    if (text[index] == '-')
    {
        negative = true;
        ++index;
    }

    double integer_part = 0;
    bool any_digits = false;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9')
    {
        integer_part = integer_part * 10 + (text[index] - '0');
        ++index;
        any_digits = true;
    }

    double fraction = 0;
    if (index < text.size() && text[index] == '.')
    {
        ++index;
        double scale = 0.1;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9')
        {
            fraction += (text[index] - '0') * scale;
            scale *= 0.1;
            ++index;
            any_digits = true;
        }
    }

    if (!any_digits || index != text.size())
    {
        return false;
    }

    out = negative ? -(integer_part + fraction) : (integer_part + fraction);
    return true;
}

bool parse_coordinate(std::string_view text, std::size_t degree_digits, unsigned max_degrees, double& out_degrees)
{
    if (text.size() <= degree_digits)
    {
        return false;
    }

    double degrees = 0;
    for (std::size_t i = 0; i < degree_digits; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return false;
        }
        degrees = degrees * 10 + (text[i] - '0');
    }

    double minutes = 0;
    if (!parse_fixed_point(text.substr(degree_digits), minutes) || minutes < 0 || minutes >= 60 ||
        degrees > max_degrees || (degrees == max_degrees && minutes != 0))
    {
        return false;
    }

    out_degrees = degrees + minutes / 60.0;
    return true;
}

bool parse_two_digit(std::string_view text, std::size_t offset, unsigned& out)
{
    if (offset + 2 > text.size())
    {
        return false;
    }
    if (text[offset] < '0' || text[offset] > '9' || text[offset + 1] < '0' || text[offset + 1] > '9')
    {
        return false;
    }
    out = static_cast<unsigned>((text[offset] - '0') * 10 + (text[offset + 1] - '0'));
    return true;
}

bool parse_time_field(std::string_view text, unsigned& hh, unsigned& mm, unsigned& ss)
{
    if (text.size() < 6 || !parse_two_digit(text, 0, hh) || !parse_two_digit(text, 2, mm) ||
        !parse_two_digit(text, 4, ss) || hh >= 24 || mm >= 60 || ss >= 60)
    {
        return false;
    }

    if (text.size() == 6)
    {
        return true;
    }

    if (text[6] != '.' || text.size() == 7)
    {
        return false;
    }

    for (std::size_t i = 7; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////

bool is_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

//////////////////////////////////////////////////////////////////////////

bool parse_date_field(std::string_view text, unsigned& day, unsigned& month, unsigned& year)
{
    if (text.size() != 6 || !parse_two_digit(text, 0, day) || !parse_two_digit(text, 2, month) ||
        !parse_two_digit(text, 4, year) || month < 1 || month > 12)
    {
        return false;
    }

    constexpr unsigned month_lengths[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const int full_year = static_cast<int>(year) + (year >= 80 ? 1900 : 2000);
    const unsigned max_day = month_lengths[month - 1] + (month == 2 && is_leap_year(full_year) ? 1 : 0);
    return day >= 1 && day <= max_day;
}

std::int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

constexpr double knots_to_mps = 0.514444;

}

namespace details
{
	result<neo6m::snapshot> from_nmea_rmc_message(const nmea_sentence& sentence)
	{
		if (sentence.type != nmea_sentence_type::rmc)
		{
			return fail(component::uart, error_code::invalid_argument);
		}

		const auto& rmc = sentence.rmc;

		neo6m::snapshot result{};
		result.received_at = xTaskGetTickCount();
		if (rmc.status != 'A' && rmc.status != 'V')
		{
			return fail(component::uart_device, error_code::invalid_argument);
		}
		result.position_valid = rmc.status == 'A';

		if (result.position_valid)
		{
			double latitude_deg = 0;
			double longitude_deg = 0;
			if (!parse_coordinate(rmc.latitude.view(), 2, 90, latitude_deg) ||
				!parse_coordinate(rmc.longitude.view(), 3, 180, longitude_deg) ||
				(rmc.lat_hemisphere != 'N' && rmc.lat_hemisphere != 'S') ||
				(rmc.lon_hemisphere != 'E' && rmc.lon_hemisphere != 'W'))
			{
				return fail(component::uart, error_code::invalid_argument);
			}

			if (rmc.lat_hemisphere == 'S')
			{
				latitude_deg = -latitude_deg;
			}
			if (rmc.lon_hemisphere == 'W')
			{
				longitude_deg = -longitude_deg;
			}

			result.latitude = latitude_deg;
			result.longitude = longitude_deg;
			if (!rmc.speed_knots.view().empty())
			{
				double speed_knots = 0;
				if (!parse_fixed_point(rmc.speed_knots.view(), speed_knots) || speed_knots < 0)
				{
					return fail(component::uart_device, error_code::invalid_argument);
				}
				result.speed_mps = static_cast<float>(speed_knots * knots_to_mps);
				result.speed_valid = true;
			}
		}

		unsigned hh = 0;
		unsigned mm = 0;
		unsigned ss = 0;
		unsigned day = 0;
		unsigned month = 0;
		unsigned year = 0;

		if (parse_time_field(rmc.utc_time.view(), hh, mm, ss) &&
			parse_date_field(rmc.date.view(), day, month, year))
		{
			const int full_year = static_cast<int>(year) + (year >= 80 ? 1900 : 2000);
			const auto days = days_from_civil(full_year, month, day);
			result.utc_time = static_cast<std::time_t>(days * 86400 + hh * 3600 + mm * 60 + ss);
			result.time_valid = true;
		}

		return result;
	}

	result<neo6m::snapshot> from_nmea_message(const nmea_sentence& sentence)
	{
		switch (sentence.type)
		{
			case nmea_sentence_type::rmc:
				return from_nmea_rmc_message(sentence);

			default:
				return fail(component::uart_device, error_code::invalid_argument);
		}
	}
}

status neo6m::init()
{
	if (initialized_)
	{
		return fail(component::uart_device, error_code::already_initialized);
	}

	initialized_ = true;
	return {};
}

status neo6m::standby_for(uint32_t duration_ms)
{
	if (!initialized_)
	{
		return fail(component::uart_device, error_code::not_initialized);
	}

	constexpr uint32_t write_timeout_ms = 100;
	constexpr uint32_t flag_backup = 0x00000002;

	uint8_t command[] = {
		0xB5, 0x62,                                     // sync chars
		0x02, 0x41,                                     // class RXM, id PMREQ
		0x08, 0x00,                                     // payload length = 8 (LE)
		static_cast<uint8_t>(duration_ms),              // duration (LE)
		static_cast<uint8_t>(duration_ms >> 8),
		static_cast<uint8_t>(duration_ms >> 16),
		static_cast<uint8_t>(duration_ms >> 24),
		static_cast<uint8_t>(flag_backup),              // flags (LE)
		static_cast<uint8_t>(flag_backup >> 8),
		static_cast<uint8_t>(flag_backup >> 16),
		static_cast<uint8_t>(flag_backup >> 24),
		0x00, 0x00                                      // CK_A, CK_B
	};

	// Fletcher-8 по class, id, length и payload (байты с 2 по 13)
	uint8_t ck_a = 0;
	uint8_t ck_b = 0;
	for (std::size_t i = 2; i < sizeof(command) - 2; ++i)
	{
		ck_a = static_cast<uint8_t>(ck_a + command[i]);
		ck_b = static_cast<uint8_t>(ck_b + ck_a);
	}
	command[sizeof(command) - 2] = ck_a;
	command[sizeof(command) - 1] = ck_b;

	if (auto result = transport_.write(command, sizeof(command), write_timeout_ms); !result)
	{
		return fail(component::uart, result.error().code);
	}

	protocol_.reset();  // выбросить недособранную NMEA-строку
	return {};
}

status neo6m::wake()
{
	if (!initialized_)
	{
		return fail(component::uart_device, error_code::not_initialized);
	}

	const uint8_t wake_byte = 0xFF;
	return transport_.write(&wake_byte, 1, 100);
}

result<neo6m::snapshot> neo6m::read_snapshot(uint32_t timeout_ms)
{
	if (!initialized_)
	{
		return fail(component::uart_device, error_code::not_initialized);
	}

	if (auto status = update_snapshot(timeout_ms); !status)
	{
		return fail(status.error().component, status.error().code);
	}

	if (!last_snapshot_)
	{
		return fail(component::uart_device, error_code::not_ready);
	}

	return last_snapshot_.value();
}

status neo6m::update_snapshot(uint32_t timeout_ms)
{
	uint8_t buffer;

	auto time_left = static_cast<int32_t>(timeout_ms);

	while (time_left > 0)
	{
		uint32_t start_tick = common::get_tick();

		auto read_result = transport_.read(&buffer, sizeof(buffer), time_left);
		if (!read_result)
		{
			return fail(component::uart, read_result.error().code);
		}
		time_left -= static_cast<int32_t>(common::ticks_to_ms(common::get_tick() - start_tick));
		if (*read_result == 0)
		{
			break;
		}

		nmea_protocol_event event = protocol_.feed(buffer);
		if (event.type == nmea_protocol_event_type::sentence)
		{
			auto snapshot = details::from_nmea_message(event.value);
			if (snapshot.has_value())
			{
				uint64_t seq = last_snapshot_ ? last_snapshot_->sequence : 0;

				last_snapshot_ = snapshot.value();
				last_snapshot_->sequence = seq + 1;
			}
		}
	}

	return {};
}
