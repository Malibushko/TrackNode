#include "infrastructure/telemetry_history_protocol.h"

namespace
{
	class response_writer
	{
	public:
		explicit response_writer(telemetry_history_protocol::response_buffer &buffer) : buffer_(buffer) {}

		void append(std::string_view text)
		{
			for (const auto character : text)
			{
				buffer_[size_++] = character;
			}
		}

		void append_hex(std::uint64_t value, std::size_t digits)
		{
			constexpr char alphabet[] = "0123456789ABCDEF";
			for (std::size_t index = digits; index > 0; --index)
			{
				buffer_[size_++] = alphabet[(value >> ((index - 1) * 4)) & 15];
			}
		}

		std::string_view finish()
		{
			append("\r\n");
			buffer_[size_] = 0;
			return {buffer_.data(), size_};
		}

	private:
		telemetry_history_protocol::response_buffer &buffer_;
		std::size_t size_ = 0;
	};

	static_assert(std::tuple_size_v<telemetry_history_protocol::response_buffer> >= 128);
}

//////////////////////////////////////////////////////////////////////////

bool telemetry_history_protocol::can_handle_command(std::string_view text)
{
	return text == "H" || text.substr(0, 2) == "H ";
}

//////////////////////////////////////////////////////////////////////////

tl::expected<telemetry_history_command, telemetry_history_parse_error> telemetry_history_protocol::parse_command(std::string_view text)
{
	const auto valid_envelope = text.size() == minimal_packet_size && text[0] == 'H' && text[1] == ' ' && text[3] == ' ';
	if (!valid_envelope)
	{
		return tl::unexpected(telemetry_history_parse_error::invalid_syntax);
	}

	telemetry_history_operation operation;
	switch (text[2])
	{
		case 'O':
			operation = telemetry_history_operation::open_snapshot;
			break;
		case 'R':
			operation = telemetry_history_operation::read_page;
			break;
		case 'A':
			operation = telemetry_history_operation::acknowledge_sample;
			break;
		default:
			return tl::unexpected(telemetry_history_parse_error::invalid_syntax);
	}

	sequence_t value = 0;
	for (const auto digit : text.substr(4))
	{
		const auto decoded = digit >= '0' && digit <= '9' ? digit - '0' :
			digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10 : -1;
		if (decoded < 0)
		{
			return tl::unexpected(telemetry_history_parse_error::invalid_number);
		}
		value = (value << 4) | static_cast<sequence_t>(decoded);
	}

	return telemetry_history_command{ .operation = operation, .value = value };
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_history_protocol::encode_snapshot(response_buffer &buffer, const telemetry_history_snapshot &snapshot)
{
	response_writer writer{buffer};
	writer.append("H O 2 ");
	for (const auto word : snapshot.device_id)
	{
		writer.append_hex(word, 8);
	}
	writer.append(" ");
	writer.append_hex(snapshot.boot_epoch, 16);
	writer.append(" ");
	writer.append_hex(snapshot.uptime_seconds, 8);
	writer.append(" ");
	writer.append_hex(snapshot.oldest_sample_id, 16);
	writer.append(" ");
	writer.append_hex(snapshot.last_sample_id, 16);
	writer.append(" ");
	writer.append_hex(snapshot.record_count, 8);
	writer.append(" ");
	writer.append_hex(snapshot.overwritten_count, 16);
	return writer.finish();
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_history_protocol::encode_page_begin(response_buffer &buffer, const telemetry_history_page_header &page)
{
	response_writer writer{buffer};
	writer.append("H B ");
	writer.append_hex(page.after_sample_id, 16);
	writer.append(" ");
	writer.append_hex(page.last_sample_id, 16);
	writer.append(" ");
	writer.append_hex(page.record_count, 8);
	return writer.finish();
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_history_protocol::encode_record(response_buffer &buffer, const telemetry_record &record)
{
	response_writer writer{buffer};
	writer.append("H D ");
	writer.append_hex(record.sample_id(), 16);
	writer.append(" ");
	for (const auto byte : record.bytes)
	{
		writer.append_hex(byte, 2);
	}
	return writer.finish();
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_history_protocol::encode_page_end(response_buffer &buffer, sequence_t last_sample_id)
{
	response_writer writer{buffer};
	writer.append("H E ");
	writer.append_hex(last_sample_id, 16);
	return writer.finish();
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_history_protocol::encode_acknowledgement(response_buffer &buffer, sequence_t last_sample_id)
{
	response_writer writer{buffer};
	writer.append("H A ");
	writer.append_hex(last_sample_id, 16);
	return writer.finish();
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_history_protocol::error_response(telemetry_history_error error)
{
	switch (error)
	{
		case telemetry_history_error::invalid_syntax:
			return "ERR HISTORY_SYNTAX\r\n";
		case telemetry_history_error::invalid_nonce:
			return "ERR HISTORY_NONCE\r\n";
		case telemetry_history_error::snapshot_not_open:
			return "ERR HISTORY_NOT_OPEN\r\n";
		case telemetry_history_error::invalid_cursor:
			return "ERR HISTORY_CURSOR\r\n";
		case telemetry_history_error::transfer_busy:
			return "ERR HISTORY_BUSY\r\n";
		case telemetry_history_error::invalid_acknowledgement:
			return "ERR HISTORY_ACK\r\n";
		case telemetry_history_error::records_overwritten:
			return "ERR HISTORY_GAP\r\n";
		case telemetry_history_error::storage_unavailable:
			return "ERR HISTORY_STORAGE\r\n";
	}
	return "ERR HISTORY_SYNTAX\r\n";
}
