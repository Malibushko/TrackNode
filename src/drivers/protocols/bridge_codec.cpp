#include "drivers/protocols/bridge_codec.h"
#include <limits>

namespace
{
constexpr auto byte_bits = std::numeric_limits<std::uint8_t>::digits;
constexpr auto checksum_high_bit = std::uint16_t{1} <<
	(std::numeric_limits<ble_bridge::checksum_type>::digits - 1);
constexpr auto full_cobs_block = std::numeric_limits<std::uint8_t>::max();

template <class integer_type>
void write_integer(std::span<std::uint8_t> output, std::size_t &position, integer_type value)
{
	for (std::size_t index = 0; index < sizeof(integer_type); ++index)
	{
		output[position++] = static_cast<std::uint8_t>(value >> (index * byte_bits));
	}
}

template <class integer_type>
integer_type read_integer(std::span<const std::uint8_t> input, std::size_t &position)
{
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < sizeof(integer_type); ++index)
	{
		value |= std::uint64_t{input[position++]} << (index * byte_bits);
	}

	return static_cast<integer_type>(value);
}

ble_bridge::checksum_type checksum(std::span<const std::uint8_t> input)
{
	static_assert(!ble_bridge::checksum_reflected);

	auto value = ble_bridge::checksum_initial;
	for (const auto byte : input)
	{
		value ^= static_cast<ble_bridge::checksum_type>(std::uint16_t{byte} << byte_bits);
		for (int bit = 0; bit < byte_bits; ++bit)
		{
			const auto high_bit = value & checksum_high_bit;
			value = static_cast<ble_bridge::checksum_type>(value << 1);
			if (high_bit)
			{
				value ^= ble_bridge::checksum_polynomial;
			}
		}
	}

	return value ^ ble_bridge::checksum_xor_out;
}

std::size_t encode_cobs(std::span<const std::uint8_t> input, std::span<std::uint8_t> output)
{
	std::size_t code_position = 0;
	std::size_t position = 1;
	std::uint8_t code = 1;

	for (const auto byte : input)
	{
		if (byte == 0)
		{
			output[code_position] = code;
			code_position = position++;
			code = 1;
		}
		else
		{
			output[position++] = byte;
			++code;
			if (code == full_cobs_block)
			{
				output[code_position] = code;
				code_position = position++;
				code = 1;
			}
		}
	}

	output[code_position] = code;
	output[position++] = 0;
	return position;
}

tl::expected<std::size_t, ble_bridge::codec_error> decode_cobs(
	std::span<const std::uint8_t> input, std::span<std::uint8_t> output)
{
	std::size_t read_position = 0;
	std::size_t write_position = 0;

	while (read_position < input.size())
	{
		const auto code = input[read_position++];
		const auto count = static_cast<std::size_t>(code) - 1;
		const auto cobs_block_truncated_or_invalid = code == 0 || count > input.size() - read_position;
		if (cobs_block_truncated_or_invalid)
		{
			return tl::make_unexpected(ble_bridge::codec_error::invalid_cobs);
		}

		if (count > output.size() - write_position)
		{
			return tl::make_unexpected(ble_bridge::codec_error::frame_too_large);
		}

		for (std::size_t index = 0; index < count; ++index)
		{
			const auto byte = input[read_position++];
			if (byte == 0)
			{
				return tl::make_unexpected(ble_bridge::codec_error::invalid_cobs);
			}

			output[write_position++] = byte;
		}

		const auto block_boundary_represents_zero = code != full_cobs_block && read_position < input.size();
		if (block_boundary_represents_zero)
		{
			if (write_position == output.size())
			{
				return tl::make_unexpected(ble_bridge::codec_error::frame_too_large);
			}

			output[write_position++] = 0;
		}
	}

	return write_position;
}
}

namespace ble_bridge
{
tl::expected<std::size_t, codec_error> bridge_codec::encode(
	const frame &message, std::span<std::uint8_t> output) const
{
	if (message.size > payload_capacity)
	{
		return tl::make_unexpected(codec_error::payload_too_large);
	}

	const auto raw_size = header_size + message.size + checksum_size;
	const auto required_capacity = raw_size + raw_size / cobs_block_capacity + cobs_code_size + delimiter_size;
	if (output.size() < required_capacity)
	{
		return tl::make_unexpected(codec_error::output_too_small);
	}

	std::array<std::uint8_t, raw_frame_capacity> raw{};
	std::size_t position = 0;
	write_integer(raw, position, version);
	write_integer(raw, position, static_cast<std::uint8_t>(message.type));
	write_integer(raw, position, message.sequence);
	write_integer(raw, position, message.session);
	write_integer(raw, position, message.size);

	for (std::size_t index = 0; index < message.size; ++index)
	{
		raw[position++] = message.payload[index];
	}

	const auto crc = checksum(std::span{raw}.first(position));
	write_integer(raw, position, crc);
	return encode_cobs(std::span{raw}.first(position), output);
}

tl::expected<frame, codec_error> bridge_codec::decode(std::span<const std::uint8_t> input) const
{
	const auto frame_delimiter_missing = input.empty() || input.back() != 0;
	if (frame_delimiter_missing)
	{
		return tl::make_unexpected(codec_error::missing_delimiter);
	}

	if (input.size() > wire_frame_capacity)
	{
		return tl::make_unexpected(codec_error::frame_too_large);
	}

	std::array<std::uint8_t, raw_frame_capacity> raw{};
	const auto decoded = decode_cobs(input.first(input.size() - delimiter_size), raw);
	if (!decoded)
	{
		return tl::make_unexpected(decoded.error());
	}

	if (*decoded < header_size + checksum_size)
	{
		return tl::make_unexpected(codec_error::frame_too_short);
	}

	const auto body_size = *decoded - checksum_size;
	auto checksum_position = body_size;
	const auto expected_checksum = read_integer<checksum_type>(raw, checksum_position);
	if (checksum(std::span{raw}.first(body_size)) != expected_checksum)
	{
		return tl::make_unexpected(codec_error::checksum_mismatch);
	}

	std::size_t position = 0;
	if (read_integer<std::uint8_t>(raw, position) != version)
	{
		return tl::make_unexpected(codec_error::unsupported_version);
	}

	frame message{};
	message.type = static_cast<frame_type>(read_integer<std::uint8_t>(raw, position));
	message.sequence = read_integer<sequence_type>(raw, position);
	message.session = read_integer<session_type>(raw, position);
	message.size = read_integer<length_type>(raw, position);

	if (message.size > payload_capacity)
	{
		return tl::make_unexpected(codec_error::payload_too_large);
	}

	if (header_size + message.size != body_size)
	{
		return tl::make_unexpected(codec_error::length_mismatch);
	}

	for (std::size_t index = 0; index < message.size; ++index)
	{
		message.payload[index] = raw[position++];
	}

	return message;
}
}
