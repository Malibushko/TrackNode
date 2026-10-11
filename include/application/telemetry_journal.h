#pragma once

#include "application/telemetry_record.h"
#include <tl/expected.hpp>

struct telemetry_journal_summary
{
	sequence_t oldest_sample_id = 0;
	sequence_t newest_sample_id = 0;
	std::size_t record_count = 0;
	std::uint64_t overwritten_count = 0;
};

struct telemetry_journal_page
{
	static constexpr std::size_t capacity = 8;
	std::size_t count = 0;
	std::array<telemetry_record, capacity> records{};

	[[nodiscard]] sequence_t last_sample_id() const
	{
		return count == 0 ? 0 : records[count - 1].sample_id();
	}
};

enum class telemetry_journal_error
{
	unordered_sample_id,
	invalid_read_range
};

template<std::size_t Capacity>
class telemetry_journal
{
	static_assert(Capacity > 0);

public:
	tl::expected<void, telemetry_journal_error> append(const telemetry_record &record)
	{
		if (record.sample_id() <= last_appended_sample_id_)
		{
			return tl::unexpected(telemetry_journal_error::unordered_sample_id);
		}
		if (count_ == Capacity)
		{
			head_ = (head_ + 1) % Capacity;
			--count_;
			++overwritten_count_;
		}
		records_[(head_ + count_) % Capacity] = record;
		++count_;
		last_appended_sample_id_ = record.sample_id();
		return {};
	}

	[[nodiscard]] tl::expected<telemetry_journal_page, telemetry_journal_error> read(
		sequence_t after_sample_id, sequence_t through_sample_id) const
	{
		if (through_sample_id < after_sample_id)
		{
			return tl::unexpected(telemetry_journal_error::invalid_read_range);
		}
		telemetry_journal_page page;
		const auto begin = find_first_after(after_sample_id);
		for (std::size_t index = begin; index < count_ && page.count < page.capacity; ++index)
		{
			const auto &record = records_[(head_ + index) % Capacity];
			if (record.sample_id() > through_sample_id)
			{
				break;
			}
			page.records[page.count++] = record;
		}
		return page;
	}

	void truncate_through(sequence_t sample_id)
	{
		const auto removed = find_first_after(sample_id);
		head_ = (head_ + removed) % Capacity;
		count_ -= removed;
	}

	void clear()
	{
		head_ = 0;
		count_ = 0;
	}

	[[nodiscard]] telemetry_journal_summary summary() const
	{
		return {
			.oldest_sample_id = count_ == 0 ? 0 : records_[head_].sample_id(),
			.newest_sample_id = count_ == 0 ? 0 : records_[(head_ + count_ - 1) % Capacity].sample_id(),
			.record_count = count_,
			.overwritten_count = overwritten_count_
		};
	}

private:
	[[nodiscard]] std::size_t find_first_after(sequence_t sample_id) const
	{
		std::size_t first = 0;
		std::size_t last = count_;
		while (first < last)
		{
			const auto middle = first + (last - first) / 2;
			if (records_[(head_ + middle) % Capacity].sample_id() <= sample_id)
			{
				first = middle + 1;
			}
			else
			{
				last = middle;
			}
		}
		return first;
	}

private:
	std::array<telemetry_record, Capacity> records_{};
	std::size_t head_ = 0;
	std::size_t count_ = 0;
	sequence_t last_appended_sample_id_ = 0;
	std::uint64_t overwritten_count_ = 0;
};
