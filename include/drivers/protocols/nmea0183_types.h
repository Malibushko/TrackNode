#pragma once
#include <array>
#include <cstddef>
#include <string_view>

enum class nmea_sentence_type
{
	unknown,
	rmc
};

enum class nmea_parse_error
{
	invalid_syntax,
	checksum_mismatch,
	unsupported_sentence,
	field_count_mismatch
};

struct talker_id
{
	char first = 0;
	char second = 0;
};

template <std::size_t N>
struct bounded_field
{
	std::array<char, N> data{};
	std::size_t length = 0;

	[[nodiscard]] std::string_view view() const { return { data.data(), length }; }
};

struct rmc_data
{
	bounded_field<12> utc_time;
	char status = 0;
	bounded_field<12> latitude;
	char lat_hemisphere = 0;
	bounded_field<12> longitude;
	char lon_hemisphere = 0;
	bounded_field<10> speed_knots;
	bounded_field<10> course_deg;
	bounded_field<6> date;
	bounded_field<10> mag_variation;
	char mag_var_direction = 0;
	char mode_indicator = 0;
};

struct nmea_sentence
{
	talker_id talker{};
	nmea_sentence_type type = nmea_sentence_type::unknown;

	union
	{
		rmc_data rmc;
	};

	nmea_sentence() : rmc{} {}
};
