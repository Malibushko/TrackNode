#include "infrastructure/telemetry_flash_journal.h"

#include <algorithm>

namespace
{
constexpr std::array<std::uint8_t, 4> journal_magic{ 'T', 'N', 'J', '1' };
constexpr std::uint8_t journal_version = 1;
constexpr std::uint32_t header_generation_offset = 8;
constexpr std::uint32_t header_session_offset = 12;
constexpr std::uint32_t header_overwritten_offset = 16;
constexpr std::uint32_t header_crc_offset = 24;
constexpr std::uint32_t record_type_offset = 0;
constexpr std::uint32_t record_state_offset = 1;
constexpr std::uint32_t record_id_offset = 2;
constexpr std::uint32_t record_payload_offset = 10;
constexpr std::uint32_t record_crc_offset = 41;
constexpr std::uint32_t record_payload_size = telemetry_record::size;
constexpr std::uint32_t crc_initial = 0xFFFFFFFF;

void put_u32(std::uint8_t *destination, std::uint32_t value)
{
	for (std::uint32_t index = 0; index < 4; ++index)
	{
		destination[index] = static_cast<std::uint8_t>(value >> (index * 8));
	}
}

void put_u64(std::uint8_t *destination, std::uint64_t value)
{
	for (std::uint32_t index = 0; index < 8; ++index)
	{
		destination[index] = static_cast<std::uint8_t>(value >> (index * 8));
	}
}

std::uint32_t get_u32(const std::uint8_t *source)
{
	return static_cast<std::uint32_t>(source[0]) |
		(static_cast<std::uint32_t>(source[1]) << 8) |
		(static_cast<std::uint32_t>(source[2]) << 16) |
		(static_cast<std::uint32_t>(source[3]) << 24);
}

std::uint64_t get_u64(const std::uint8_t *source)
{
	std::uint64_t value = 0;
	for (std::uint32_t index = 0; index < 8; ++index)
	{
		value |= static_cast<std::uint64_t>(source[index]) << (index * 8);
	}
	return value;
}

bool is_erased(std::span<const std::uint8_t> bytes)
{
	return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t value) { return value == 0xFF; });
}

std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::uint8_t> bytes)
{
	for (const auto value : bytes)
	{
		crc ^= value;
		for (std::uint32_t bit = 0; bit < 8; ++bit)
		{
			const auto mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1)));
			crc = (crc >> 1) ^ (0xEDB88320 & mask);
		}
	}
	return crc;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes)
{
	return ~crc32_update(crc_initial, bytes);
}

std::uint32_t record_crc(const std::uint8_t *bytes)
{
	const auto with_type = crc32_update(crc_initial, { bytes + record_type_offset, 1 });
	return ~crc32_update(with_type, { bytes + record_id_offset, record_crc_offset - record_id_offset });
}

sequence_t record_sample_id(const std::uint8_t *bytes)
{
	return get_u64(bytes + record_id_offset);
}
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::init(w25qxx &flash)
{
	if (initialized_)
	{
		return fail(component::storage, error_code::already_initialized);
	}
	if (auto started = flash.init(); !started)
	{
		return started;
	}
	flash_ = &flash;
	sector_count_ = flash.capacity_bytes() / w25qxx::sector_size;
	if (sector_count_ < 2)
	{
		return fail(component::storage, error_code::invalid_config);
	}
	if (auto scanned = scan(); !scanned)
	{
		return scanned;
	}
	const auto newest_session_id = std::max(newest_header_session_id_, static_cast<std::uint32_t>(latest_sample_id_ >> 32));
	if (newest_session_id == UINT32_MAX)
	{
		return fail(component::storage, error_code::storage_error);
	}
	session_id_ = newest_session_id + 1;
	initialized_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::append(const telemetry_record &record)
{
	if (!initialized_)
	{
		return fail(component::storage, error_code::not_initialized);
	}
	const auto sample_id = record.sample_id();
	const auto belongs_to_current_session = (sample_id >> 32) == session_id_ && static_cast<std::uint32_t>(sample_id) != 0;
	if (sample_id <= latest_sample_id_ || !belongs_to_current_session)
	{
		return fail(component::storage, error_code::invalid_argument);
	}
	const auto needs_fresh_sector = latest_generation_ == 0 || current_slot_ >= records_per_sector;
	if (needs_fresh_sector)
	{
		if (auto rotated = rotate_sector(); !rotated)
		{
			return rotated;
		}
	}
	return append_record(record);
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::acknowledge(sequence_t sample_id)
{
	if (!initialized_)
	{
		return fail(component::storage, error_code::not_initialized);
	}
	if (sample_id == 0)
	{
		return fail(component::storage, error_code::invalid_argument);
	}
	if (summary_.record_count == 0 || sample_id < summary_.oldest_sample_id)
	{
		return {};
	}
	std::size_t acknowledged_count = 0;
	std::uint32_t next_generation = 0;
	std::uint32_t next_slot = 0;
	sequence_t next_sample_id = 0;
	for (auto generation = oldest_pending_generation_; generation <= latest_generation_ && next_generation == 0; ++generation)
	{
		bool belongs_to_generation = false;
		if (auto read = read_sector(generation, belongs_to_generation); !read)
		{
			summary_.record_count -= std::min(acknowledged_count, summary_.record_count);
			return read;
		}
		if (!belongs_to_generation)
		{
			continue;
		}
		const auto first_slot = generation == oldest_pending_generation_ ? oldest_pending_slot_ : 0;
		for (std::uint32_t slot = first_slot; slot < records_per_sector; ++slot)
		{
			const auto *bytes = slot_bytes(slot);
			if (!restorable_record(bytes))
			{
				break;
			}
			if (!pending_record(bytes))
			{
				continue;
			}
			const auto id = record_sample_id(bytes);
			if (id > sample_id)
			{
				next_generation = generation;
				next_slot = slot;
				next_sample_id = id;
				break;
			}
			if (auto acknowledged = update_acknowledgement(generation, slot); !acknowledged)
			{
				summary_.record_count -= std::min(acknowledged_count, summary_.record_count);
				return acknowledged;
			}
			++acknowledged_count;
		}
	}
	summary_.record_count -= std::min(acknowledged_count, summary_.record_count);
	const auto pending_records_remain = summary_.record_count != 0 && next_generation != 0;
	if (!pending_records_remain)
	{
		clear_pending();
		return {};
	}
	oldest_pending_generation_ = next_generation;
	oldest_pending_slot_ = next_slot;
	summary_.oldest_sample_id = next_sample_id;
	return {};
}

//////////////////////////////////////////////////////////////////////////

result<telemetry_journal_page> telemetry_flash_journal::read(sequence_t after_sample_id,
	                                                         sequence_t through_sample_id)
{
	if (!initialized_)
	{
		return tl::unexpected(error{ component::storage, error_code::not_initialized });
	}
	if (through_sample_id < after_sample_id)
	{
		return tl::unexpected(error{ component::storage, error_code::invalid_argument });
	}
	telemetry_journal_page page{};
	if (summary_.record_count == 0)
	{
		return page;
	}
	bool passed_requested_range = false;
	for (auto generation = oldest_pending_generation_;
		generation <= latest_generation_ && page.count < page.capacity && !passed_requested_range; ++generation)
	{
		bool belongs_to_generation = false;
		if (auto read = read_sector(generation, belongs_to_generation); !read)
		{
			return tl::unexpected(read.error());
		}
		if (!belongs_to_generation)
		{
			continue;
		}
		const auto first_slot = generation == oldest_pending_generation_ ? oldest_pending_slot_ : 0;
		for (std::uint32_t slot = first_slot; slot < records_per_sector && page.count < page.capacity; ++slot)
		{
			const auto *bytes = slot_bytes(slot);
			if (!restorable_record(bytes))
			{
				break;
			}
			const auto sample_id = record_sample_id(bytes);
			if (sample_id > through_sample_id)
			{
				passed_requested_range = true;
				break;
			}
			if (!pending_record(bytes) || sample_id <= after_sample_id)
			{
				continue;
			}
			telemetry_record record{};
			std::copy_n(bytes + record_payload_offset, record_payload_size, record.bytes.begin());
			page.records[page.count++] = record;
		}
	}
	return page;
}

//////////////////////////////////////////////////////////////////////////

telemetry_journal_summary telemetry_flash_journal::summary() const
{
	return summary_;
}

//////////////////////////////////////////////////////////////////////////

sequence_t telemetry_flash_journal::next_sample_id() const
{
	return (static_cast<sequence_t>(session_id_) << 32) | 1;
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::scan()
{
	latest_generation_ = 0;
	current_slot_ = records_per_sector;
	newest_header_session_id_ = 0;
	latest_sample_id_ = 0;
	clear_pending();
	summary_ = {};

	sector_header newest{};
	for (std::uint32_t index = 0; index < sector_count_; ++index)
	{
		sector_header header{};
		bool valid = false;
		if (auto read = read_header(index, header, valid); !read)
		{
			return read;
		}
		if (valid && header.generation > latest_generation_)
		{
			latest_generation_ = header.generation;
			newest = header;
		}
	}
	if (latest_generation_ == 0)
	{
		return {};
	}

	newest_header_session_id_ = newest.session_id;
	summary_.overwritten_count = newest.overwritten_count;
	for (auto generation = first_live_generation(latest_generation_); generation <= latest_generation_; ++generation)
	{
		if (auto restored = restore_sector(generation); !restored)
		{
			return restored;
		}
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::restore_sector(std::uint32_t generation)
{
	bool belongs_to_generation = false;
	if (auto read = read_sector(generation, belongs_to_generation); !read)
	{
		return read;
	}
	if (!belongs_to_generation)
	{
		return {};
	}
	const auto is_latest_sector = generation == latest_generation_;
	for (std::uint32_t slot = 0; slot < records_per_sector; ++slot)
	{
		const auto *bytes = slot_bytes(slot);
		if (is_erased({ bytes, record_size }))
		{
			if (is_latest_sector)
			{
				current_slot_ = slot;
			}
			return {};
		}
		telemetry_record record{};
		std::copy_n(bytes + record_payload_offset, record_payload_size, record.bytes.begin());
		const auto sample_id = record_sample_id(bytes);
		const auto continues_sequence = restorable_record(bytes) && sample_id > latest_sample_id_ &&
			record.sample_id() == sample_id;
		if (!continues_sequence)
		{
			return {};
		}
		latest_sample_id_ = sample_id;
		if (pending_record(bytes))
		{
			if (summary_.record_count == 0)
			{
				summary_.oldest_sample_id = sample_id;
				oldest_pending_generation_ = generation;
				oldest_pending_slot_ = slot;
			}
			++summary_.record_count;
			summary_.newest_sample_id = sample_id;
		}
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::rotate_sector()
{
	if (latest_generation_ == UINT32_MAX)
	{
		return fail(component::storage, error_code::storage_error);
	}
	const auto generation = latest_generation_ + 1;
	const auto index = sector_index(generation);
	const auto address = index * w25qxx::sector_size;

	sector_header previous{};
	bool previous_valid = false;
	if (auto read = read_header(index, previous, previous_valid); !read)
	{
		return read;
	}
	std::size_t dropped_count = 0;
	bool dropped_oldest = false;
	if (previous_valid)
	{
		if (auto read = flash_->read(address, sector_buffer_); !read)
		{
			return read;
		}
		for (std::uint32_t slot = 0; slot < records_per_sector; ++slot)
		{
			const auto *bytes = slot_bytes(slot);
			if (!restorable_record(bytes))
			{
				break;
			}
			if (pending_record(bytes))
			{
				dropped_oldest = dropped_oldest || record_sample_id(bytes) == summary_.oldest_sample_id;
				++dropped_count;
			}
		}
	}
	if (auto erased = flash_->erase_sector(address); !erased)
	{
		return erased;
	}

	summary_.overwritten_count += dropped_count;
	summary_.record_count -= std::min(dropped_count, summary_.record_count);
	if (summary_.record_count == 0)
	{
		clear_pending();
	}
	else if (dropped_oldest)
	{
		if (auto located = locate_oldest_pending(first_live_generation(generation), generation - 1); !located)
		{
			return located;
		}
	}

	if (auto header = write_header(generation); !header)
	{
		return header;
	}
	latest_generation_ = generation;
	current_slot_ = 0;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::locate_oldest_pending(std::uint32_t first_generation, std::uint32_t last_generation)
{
	for (auto generation = first_generation; generation <= last_generation; ++generation)
	{
		bool belongs_to_generation = false;
		if (auto read = read_sector(generation, belongs_to_generation); !read)
		{
			return read;
		}
		if (!belongs_to_generation)
		{
			continue;
		}
		for (std::uint32_t slot = 0; slot < records_per_sector; ++slot)
		{
			const auto *bytes = slot_bytes(slot);
			if (!restorable_record(bytes))
			{
				break;
			}
			if (pending_record(bytes))
			{
				oldest_pending_generation_ = generation;
				oldest_pending_slot_ = slot;
				summary_.oldest_sample_id = record_sample_id(bytes);
				return {};
			}
		}
	}
	clear_pending();
	return {};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::append_record(const telemetry_record &record)
{
	std::array<std::uint8_t, record_size> bytes{};
	bytes.fill(0xFF);
	bytes[record_type_offset] = sample_record_type;
	bytes[record_state_offset] = pending_state;
	put_u64(bytes.data() + record_id_offset, record.sample_id());
	std::copy(record.bytes.begin(), record.bytes.end(), bytes.begin() + record_payload_offset);
	put_u32(bytes.data() + record_crc_offset, record_crc(bytes.data()));

	const auto slot = current_slot_;
	const auto address = record_address(latest_generation_, slot);
	const std::uint8_t marker = committed_marker;
	auto written = flash_->program(address, { bytes.data(), record_size - 1 })
		.and_then([&]{ return flash_->program(address + record_size - 1, { &marker, 1 }); });
	if (!written)
	{
		current_slot_ = records_per_sector;
		return written;
	}

	current_slot_ = slot + 1;
	latest_sample_id_ = record.sample_id();
	if (summary_.record_count == 0)
	{
		summary_.oldest_sample_id = record.sample_id();
		oldest_pending_generation_ = latest_generation_;
		oldest_pending_slot_ = slot;
	}
	++summary_.record_count;
	summary_.newest_sample_id = record.sample_id();
	return {};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::update_acknowledgement(std::uint32_t generation, std::uint32_t slot)
{
	const auto address = record_address(generation, slot) + record_state_offset;
	return flash_->program(address, { &acknowledged_state, 1 });
}

//////////////////////////////////////////////////////////////////////////

void telemetry_flash_journal::clear_pending()
{
	summary_.record_count = 0;
	summary_.oldest_sample_id = 0;
	summary_.newest_sample_id = 0;
	oldest_pending_generation_ = 0;
	oldest_pending_slot_ = 0;
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::read_sector(std::uint32_t generation, bool &belongs_to_generation)
{
	belongs_to_generation = false;
	if (!flash_ || generation == 0)
	{
		return fail(component::storage, error_code::invalid_argument);
	}
	if (auto read = flash_->read(sector_index(generation) * w25qxx::sector_size, sector_buffer_); !read)
	{
		return read;
	}
	belongs_to_generation = header_valid(sector_buffer_.data()) &&
		get_u32(sector_buffer_.data() + header_generation_offset) == generation;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::read_header(std::uint32_t index, sector_header &header, bool &valid) const
{
	std::array<std::uint8_t, header_size> bytes{};
	if (auto read = flash_->read(index * w25qxx::sector_size, bytes); !read)
	{
		return read;
	}
	valid = header_valid(bytes.data());
	if (!valid)
	{
		return {};
	}
	header.generation = get_u32(bytes.data() + header_generation_offset);
	header.session_id = get_u32(bytes.data() + header_session_offset);
	header.overwritten_count = get_u64(bytes.data() + header_overwritten_offset);
	return {};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_flash_journal::write_header(std::uint32_t generation)
{
	std::array<std::uint8_t, header_size> bytes{};
	bytes.fill(0xFF);
	std::copy(journal_magic.begin(), journal_magic.end(), bytes.begin());
	bytes[4] = journal_version;
	put_u32(bytes.data() + header_generation_offset, generation);
	put_u32(bytes.data() + header_session_offset, session_id_);
	put_u64(bytes.data() + header_overwritten_offset, summary_.overwritten_count);
	put_u32(bytes.data() + header_crc_offset, crc32({ bytes.data(), header_crc_offset }));

	const auto address = sector_index(generation) * w25qxx::sector_size;
	if (auto written = flash_->program(address, { bytes.data(), bytes.size() - 1 }); !written)
	{
		return written;
	}
	const std::uint8_t marker = committed_marker;
	return flash_->program(address + header_size - 1, { &marker, 1 });
}

//////////////////////////////////////////////////////////////////////////

bool telemetry_flash_journal::restorable_record(const std::uint8_t *bytes) const
{
	return record_valid(bytes) && bytes[record_type_offset] == sample_record_type;
}

//////////////////////////////////////////////////////////////////////////

bool telemetry_flash_journal::pending_record(const std::uint8_t *bytes) const
{
	return bytes[record_state_offset] == pending_state;
}

//////////////////////////////////////////////////////////////////////////

const std::uint8_t *telemetry_flash_journal::slot_bytes(std::uint32_t slot) const
{
	return sector_buffer_.data() + header_size + slot * record_size;
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t telemetry_flash_journal::first_live_generation(std::uint32_t newest_generation) const
{
	return newest_generation >= sector_count_ ? newest_generation - sector_count_ + 1 : 1;
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t telemetry_flash_journal::sector_index(std::uint32_t generation) const
{
	return (generation - 1) % sector_count_;
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t telemetry_flash_journal::record_address(std::uint32_t generation, std::uint32_t slot) const
{
	return sector_index(generation) * w25qxx::sector_size + header_size + slot * record_size;
}

//////////////////////////////////////////////////////////////////////////

bool telemetry_flash_journal::header_valid(const std::uint8_t *bytes) const
{
	return std::equal(journal_magic.begin(), journal_magic.end(), bytes) && bytes[4] == journal_version &&
		bytes[header_size - 1] == committed_marker &&
		get_u32(bytes + header_generation_offset) != 0 && get_u32(bytes + header_session_offset) != 0 &&
		get_u32(bytes + header_crc_offset) == crc32({ bytes, header_crc_offset });
}

//////////////////////////////////////////////////////////////////////////

bool telemetry_flash_journal::record_valid(const std::uint8_t *bytes) const
{
	return bytes[record_size - 1] == committed_marker &&
		(bytes[record_state_offset] == pending_state || bytes[record_state_offset] == acknowledged_state) &&
		get_u32(bytes + record_crc_offset) == record_crc(bytes);
}

//////////////////////////////////////////////////////////////////////////
