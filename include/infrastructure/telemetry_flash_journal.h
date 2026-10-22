#pragma once

#include "application/telemetry_journal.h"
#include "drivers/devices/w25qxx.h"

#include <array>

class telemetry_flash_journal
{
public:
	status init(w25qxx &flash);
	status append(const telemetry_record &record);
	status acknowledge(sequence_t sample_id);
	result<telemetry_journal_page> read(sequence_t after_sample_id, sequence_t through_sample_id);

	[[nodiscard]] telemetry_journal_summary summary() const;
	[[nodiscard]] sequence_t next_sample_id() const;
	[[nodiscard]] std::uint32_t session_id() const { return session_id_; }

private:
	struct sector_header
	{
		std::uint32_t generation = 0;
		std::uint32_t session_id = 0;
		std::uint64_t overwritten_count = 0;
	};

	status scan();
	status restore_sector(std::uint32_t generation);
	status rotate_sector();
	status locate_oldest_pending(std::uint32_t first_generation, std::uint32_t last_generation);
	status append_record(const telemetry_record &record);
	status update_acknowledgement(std::uint32_t generation, std::uint32_t slot);
	void clear_pending();

	status read_sector(std::uint32_t generation, bool &belongs_to_generation);
	status read_header(std::uint32_t sector_index, sector_header &header, bool &valid) const;
	status write_header(std::uint32_t generation);

	[[nodiscard]] bool restorable_record(const std::uint8_t *bytes) const;
	[[nodiscard]] bool pending_record(const std::uint8_t *bytes) const;
	[[nodiscard]] const std::uint8_t *slot_bytes(std::uint32_t slot) const;
	[[nodiscard]] std::uint32_t first_live_generation(std::uint32_t newest_generation) const;
	[[nodiscard]] std::uint32_t sector_index(std::uint32_t generation) const;
	[[nodiscard]] std::uint32_t record_address(std::uint32_t generation, std::uint32_t slot) const;
	[[nodiscard]] bool header_valid(const std::uint8_t *bytes) const;
	[[nodiscard]] bool record_valid(const std::uint8_t *bytes) const;

	static constexpr std::uint32_t header_size = 64;
	static constexpr std::uint32_t record_size = 64;
	static constexpr std::uint32_t records_per_sector = (w25qxx::sector_size - header_size) / record_size;
	static constexpr std::uint8_t sample_record_type = 0x01;
	static constexpr std::uint8_t pending_state = 0xFF;
	static constexpr std::uint8_t acknowledged_state = 0xFE;
	static constexpr std::uint8_t committed_marker = 0x00;

private:
	w25qxx *flash_ = nullptr;
	std::uint32_t sector_count_ = 0;
	std::uint32_t latest_generation_ = 0;
	std::uint32_t current_slot_ = 0;
	std::uint32_t oldest_pending_generation_ = 0;
	std::uint32_t oldest_pending_slot_ = 0;
	std::uint32_t newest_header_session_id_ = 0;
	std::uint32_t session_id_ = 0;
	sequence_t latest_sample_id_ = 0;
	telemetry_journal_summary summary_{};
	std::array<std::uint8_t, w25qxx::sector_size> sector_buffer_{};
	bool initialized_ = false;
};
