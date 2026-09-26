#include "drivers/protocols/nmea0183.h"
#include <tl/expected.hpp>

namespace
{

constexpr std::size_t max_fields = 16;

std::uint8_t hex_digit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'A' && c <= 'F')
    {
        return static_cast<std::uint8_t>(c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f')
    {
        return static_cast<std::uint8_t>(c - 'a' + 10);
    }
    return 0xff;
}

//////////////////////////////////////////////////////////////////////////

std::size_t split_fields(std::string_view payload, std::array<std::string_view, max_fields>& fields)
{
    std::size_t count = 0;
    std::size_t start = 0;
    while (count < max_fields)
    {
        const auto comma = payload.find(',', start);
        if (comma == std::string_view::npos)
        {
            fields[count++] = payload.substr(start);
            break;
        }
        fields[count++] = payload.substr(start, comma - start);
        start = comma + 1;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////

template <std::size_t N>
bool assign_field(std::string_view src, bounded_field<N>& field)
{
    if (src.size() > N)
    {
        return false;
    }
    for (std::size_t i = 0; i < src.size(); ++i)
    {
        field.data[i] = src[i];
    }
    field.length = src.size();
    return true;
}

//////////////////////////////////////////////////////////////////////////

char first_char(std::string_view field)
{
    return field.empty() ? char{ 0 } : field[0];
}

//////////////////////////////////////////////////////////////////////////

}

tl::expected<nmea_sentence, nmea_parse_error> parse_sentence(std::string_view line)
{
    if (line.size() < 6 || (line[0] != '$' && line[0] != '!'))
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    const auto star = line.rfind('*');
    if (star == std::string_view::npos)
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    if (line.size() - star != 3)
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    const auto high = hex_digit(line[star + 1]);
    const auto low = hex_digit(line[star + 2]);
    if (high == 0xff || low == 0xff)
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    const auto expected_checksum = static_cast<std::uint8_t>((high << 4) | low);

    std::uint8_t computed_checksum = 0;
    for (std::size_t i = 1; i < star; ++i)
    {
        computed_checksum ^= static_cast<std::uint8_t>(line[i]);
    }

    if (computed_checksum != expected_checksum)
    {
        return tl::make_unexpected(nmea_parse_error::checksum_mismatch);
    }

    const auto payload = line.substr(1, star - 1);

    std::array<std::string_view, max_fields> fields{};
    const auto field_count = split_fields(payload, fields);

    if (fields[0].size() != 5)
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    nmea_sentence sentence{};
    sentence.talker = { fields[0][0], fields[0][1] };
    const auto id = fields[0].substr(2, 3);

    if (id != "RMC")
    {
        return tl::make_unexpected(nmea_parse_error::unsupported_sentence);
    }

    sentence.type = nmea_sentence_type::rmc;

    if (field_count != 13)
    {
        return tl::make_unexpected(nmea_parse_error::field_count_mismatch);
    }

    auto& rmc = sentence.rmc;
    if (!assign_field(fields[1], rmc.utc_time))
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    rmc.status = first_char(fields[2]);
    if (!assign_field(fields[3], rmc.latitude))
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    rmc.lat_hemisphere = first_char(fields[4]);
    if (!assign_field(fields[5], rmc.longitude))
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    rmc.lon_hemisphere = first_char(fields[6]);
    if (!assign_field(fields[7], rmc.speed_knots))
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    if (!assign_field(fields[8], rmc.course_deg))
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    if (!assign_field(fields[9], rmc.date))
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    if (!assign_field(fields[10], rmc.mag_variation))
    {
        return tl::make_unexpected(nmea_parse_error::invalid_syntax);
    }

    rmc.mag_var_direction = first_char(fields[11]);
    rmc.mode_indicator = first_char(fields[12]);

    return sentence;
}

//////////////////////////////////////////////////////////////////////////

nmea_protocol_event nmea_protocol::feed(std::uint8_t byte)
{
	if (byte != '\r' && byte != '\n')
	{
		if (!discarding_)
		{
			if (length_ == line_.size())
			{
				discarding_ = true;
			}
			else
			{
				line_[length_++] = static_cast<char>(byte);
			}
		}
		return {};
	}

	if (discarding_)
	{
		discarding_ = false;
		length_ = 0;
		return { nmea_protocol_event_type::line_too_long };
	}

	bool blank = true;
	for (std::size_t index = 0; index < length_; ++index)
	{
		if (line_[index] != ' ' && line_[index] != '\t')
		{
			blank = false;
			break;
		}
	}
	if (blank)
	{
		length_ = 0;
		return {};
	}

	const auto parsed = parse_sentence({ line_.data(), length_ });
	length_ = 0;
	if (!parsed)
	{
		return { nmea_protocol_event_type::parse_error, {}, parsed.error() };
	}
    return { nmea_protocol_event_type::sentence, *parsed };
}

//////////////////////////////////////////////////////////////////////////

void nmea_protocol::reset()
{
	length_ = 0;
    discarding_ = false;
}

//////////////////////////////////////////////////////////////////////////
