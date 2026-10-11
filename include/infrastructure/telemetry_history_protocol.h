#pragma once

#include "application/telemetry_record.h"
#include <string_view>
#include <tl/expected.hpp>

enum class telemetry_history_operation
{
	open_snapshot,
	read_page,
	acknowledge_sample
};

struct telemetry_history_command
{
	telemetry_history_operation operation;
	sequence_t value;
};

enum class telemetry_history_parse_error
{
	invalid_syntax,
	invalid_number
};

struct telemetry_history_snapshot
{
	std::array<std::uint32_t, 3> device_id;
	std::uint64_t boot_epoch;
	std::uint32_t uptime_seconds;
	sequence_t oldest_sample_id;
	sequence_t last_sample_id;
	std::uint32_t record_count;
	std::uint64_t overwritten_count;
};

struct telemetry_history_page_header
{
	sequence_t after_sample_id;
	sequence_t last_sample_id;
	std::uint32_t record_count;
};

enum class telemetry_history_error
{
	invalid_syntax,
	invalid_nonce,
	snapshot_not_open,
	invalid_cursor,
	transfer_busy,
	invalid_acknowledgement,
	records_overwritten
};

class telemetry_history_protocol
{
public:
	static constexpr std::size_t minimal_packet_size = 20;

	[[nodiscard]] static bool can_handle_command(std::string_view text);
	[[nodiscard]] static tl::expected<telemetry_history_command, telemetry_history_parse_error> parse_command(std::string_view text);

	using response_buffer = std::array<char, 160>;
	[[nodiscard]] static std::string_view encode_snapshot(response_buffer &buffer, const telemetry_history_snapshot &snapshot);
	[[nodiscard]] static std::string_view encode_page_begin(response_buffer &buffer, const telemetry_history_page_header &page);
	[[nodiscard]] static std::string_view encode_record(response_buffer &buffer, const telemetry_record &record);
	[[nodiscard]] static std::string_view encode_page_end(response_buffer &buffer, sequence_t last_sample_id);
	[[nodiscard]] static std::string_view encode_acknowledgement(response_buffer &buffer, sequence_t last_sample_id);
	[[nodiscard]] static std::string_view error_response(telemetry_history_error error);
};
