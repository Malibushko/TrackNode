#include "infrastructure/telemetry_journal_service.h"
#include "semphr.h"
#include "task.h"
#include "libs/freertos/freertos_helpers.h"
#include <algorithm>
#include <cmath>

namespace
{
	class journal_lock
	{
	public:
		explicit journal_lock(SemaphoreHandle_t mutex) : mutex_(mutex)
		{
			if (mutex_)
			{
				xSemaphoreTake(mutex_, portMAX_DELAY);
			}
		}
		~journal_lock()
		{
			if (mutex_)
			{
				xSemaphoreGive(mutex_);
			}
		}
	private:
		SemaphoreHandle_t mutex_;
	};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_journal_service::init()
{
	if (lock_)
	{
		return fail(component::service, error_code::already_initialized);
	}
	lock_ = xSemaphoreCreateMutex();
	if (!lock_)
	{
		return fail(component::rtos, error_code::out_of_memory);
	}
	if (auto initialized = journal_.init(flash_); !initialized)
	{
		storage_failure_ = initialized.error();
		return {};
	}
	storage_available_ = true;
	boot_epoch_ = journal_.session_id();
	last_received_sample_id_ = journal_.next_sample_id() - 1;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status telemetry_journal_service::submit(const telemetry_sample &sample)
{
	const auto archived = archive(sample);
	const auto forwarded = downstream_.submit(sample);
	return archived ? forwarded : archived;
}

//////////////////////////////////////////////////////////////////////////

status telemetry_journal_service::acknowledge(sequence_t sample_id)
{
	journal_lock lock{lock_};
	if (!storage_available_)
	{
		return fail(component::service, error_code::not_ready);
	}
	if (sample_id == 0)
	{
		return fail(component::service, error_code::invalid_argument);
	}
	if (sample_id <= confirmed_through_sample_id_)
	{
		return {};
	}
	const auto summary = journal_.summary();
	const auto acknowledged_sample_already_overwritten = summary.record_count != 0 && sample_id < summary.oldest_sample_id;
	if (acknowledged_sample_already_overwritten)
	{
		confirmed_through_sample_id_ = sample_id;
		return {};
	}
	const auto matching = journal_.read(sample_id - 1, sample_id);
	const auto confirms_archived_sample = matching && matching->count == 1 && matching->records[0].sample_id() == sample_id;
	if (!confirms_archived_sample)
	{
		return fail(component::service, error_code::invalid_argument);
	}
	if (auto acknowledged = journal_.acknowledge(sample_id); !acknowledged)
	{
		return acknowledged;
	}
	confirmed_through_sample_id_ = sample_id;
	return {};
}

//////////////////////////////////////////////////////////////////////////

void telemetry_journal_service::set_power_mode(power_mode mode)
{
	downstream_.set_power_mode(mode);
}

//////////////////////////////////////////////////////////////////////////

bool telemetry_journal_service::can_handle_command(std::string_view text) const
{
	return telemetry_history_protocol::can_handle_command(text);
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_journal_service::handle_history_command(std::string_view text, std::uint32_t session)
{
	const auto command = telemetry_history_protocol::parse_command(text);
	if (!command)
	{
		return telemetry_history_protocol::error_response(telemetry_history_error::invalid_syntax);
	}
	if (!storage_available_)
	{
		const auto failure = storage_failure_.value_or(error{ component::storage, error_code::not_initialized });
		return telemetry_history_protocol::encode_storage_failure(response_, {
			.component = static_cast<std::uint8_t>(failure.component),
			.code = static_cast<std::uint8_t>(failure.code),
			.jedec_id = flash_.jedec_id()
		});
	}
	if (sending_page_)
	{
		return telemetry_history_protocol::error_response(telemetry_history_error::transfer_busy);
	}
	if (command->operation == telemetry_history_operation::open_snapshot)
	{
		return handle_open_snapshot(command->value, session);
	}
	const auto belongs_to_current_sync = sync_session_ != 0 && sync_session_ == session;
	if (!belongs_to_current_sync)
	{
		return telemetry_history_protocol::error_response(telemetry_history_error::snapshot_not_open);
	}

	switch (command->operation)
	{
		case telemetry_history_operation::read_page:
			return handle_read_page(command->value);
		case telemetry_history_operation::acknowledge_sample:
			return handle_history_ack(command->value);
		default:
			return telemetry_history_protocol::error_response(telemetry_history_error::invalid_syntax);
	}
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_journal_service::next_history_response()
{
	if (!sending_page_)
	{
		return {};
	}
	if (response_index_ == 0)
	{
		return telemetry_history_protocol::encode_page_begin(response_, {
			.after_sample_id = read_after_sample_id_,
			.last_sample_id = page_.last_sample_id(),
			.record_count = static_cast<std::uint32_t>(page_.count)
		});
	}
	if (response_index_ <= page_.count)
	{
		return telemetry_history_protocol::encode_record(response_, page_.records[response_index_ - 1]);
	}
	return telemetry_history_protocol::encode_page_end(response_, page_.last_sample_id());
}

//////////////////////////////////////////////////////////////////////////

void telemetry_journal_service::on_history_response_sent()
{
	if (!sending_page_)
	{
		return;
	}
	++response_index_;
	const auto page_end_transmitted = response_index_ > page_.count + 1;
	if (page_end_transmitted)
	{
		sending_page_ = false;
		page_transmitted_ = true;
	}
}

//////////////////////////////////////////////////////////////////////////

void telemetry_journal_service::reset_history_transfer()
{
	sync_session_ = 0;
	sending_page_ = false;
	page_transmitted_ = false;
}

//////////////////////////////////////////////////////////////////////////

std::uint64_t telemetry_journal_service::update_uptime()
{
	const auto tick = xTaskGetTickCount();
	elapsed_ticks_ += static_cast<TickType_t>(tick - previous_tick_);
	previous_tick_ = tick;
	return elapsed_ticks_;
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t telemetry_journal_service::sample_seconds(TickType_t sample_tick, std::uint64_t uptime) const
{
	const auto age_ticks = static_cast<TickType_t>(static_cast<TickType_t>(uptime) - sample_tick);
	return static_cast<std::uint32_t>((uptime - std::min<std::uint64_t>(age_ticks, uptime)) / configTICK_RATE_HZ);
}

//////////////////////////////////////////////////////////////////////////

bool telemetry_journal_service::is_representable_environment(const sensor_sample &environment) const
{
	return environment.valid && std::isfinite(environment.temperature_c) &&
	       std::isfinite(environment.pressure_pa) && environment.temperature_c >= -327.68f && environment.temperature_c <= 327.67f &&
	       environment.pressure_pa >= 0 && environment.pressure_pa <= 200000;
}

//////////////////////////////////////////////////////////////////////////

telemetry_record telemetry_journal_service::pack_sample(const telemetry_sample &sample, std::uint64_t uptime) const
{
	telemetry_record record;
	record.set_identity(sample.id, sample_seconds(sample.sensor.timestamp, uptime));
	const auto &environment = sample.sensor;
	const auto representable_environment = is_representable_environment(environment);

	if (representable_environment)
	{
		record.set_environment(static_cast<std::int16_t>(std::lround(environment.temperature_c * 100)), static_cast<std::uint32_t>(std::lround(environment.pressure_pa)));
	}

	const auto fresh_position = sample.gnss && sample.gnss->position && static_cast<TickType_t>(uptime) - sample.gnss->received_at <= pdMS_TO_TICKS(gnss_freshness_limit_ms);
	if (fresh_position)
	{
		const auto &position = *sample.gnss->position;
		const auto representable_position = std::isfinite(position.latitude) && std::isfinite(position.longitude) &&
			std::abs(position.latitude) <= 90 && std::abs(position.longitude) <= 180;
		if (representable_position)
		{
			record.set_position(sample_seconds(sample.gnss->received_at, uptime),
				static_cast<std::int32_t>(std::llround(position.latitude * 1e7)),
				static_cast<std::int32_t>(std::llround(position.longitude * 1e7)));
		}
	}
	return record;
}

//////////////////////////////////////////////////////////////////////////

status telemetry_journal_service::archive(const telemetry_sample &sample)
{
	if (!storage_available_)
	{
		return {};
	}
	const auto sample_id_not_increasing = sample.id == 0 || sample.id <= last_received_sample_id_;
	if (sample_id_not_increasing)
	{
		return fail(component::service, error_code::invalid_argument);
	}
	std::uint64_t uptime;
	{
		journal_lock lock{lock_};
		uptime = update_uptime();
	}
	const auto record = pack_sample(sample, uptime);
	if (policy_.should_archive(record))
	{
		{
			journal_lock lock{lock_};
			if (auto appended = journal_.append(record); !appended)
			{
				return appended;
			}
		}
		policy_.on_archived(record);
	}
	last_received_sample_id_ = sample.id;
	return {};
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_journal_service::handle_open_snapshot(sequence_t nonce, std::uint32_t session)
{
	if (nonce == 0 || session == 0)
	{
		return telemetry_history_protocol::error_response(telemetry_history_error::invalid_nonce);
	}
	telemetry_journal_summary summary;
	std::uint32_t seconds;
	{
		journal_lock lock{lock_};
		seconds = static_cast<std::uint32_t>(update_uptime() / configTICK_RATE_HZ);
		summary = journal_.summary();
	}
	sync_session_ = session;
	read_after_sample_id_ = 0;
	snapshot_last_sample_id_ = summary.newest_sample_id;
	snapshot_overwritten_count_ = summary.overwritten_count;
	page_transmitted_ = false;

	return telemetry_history_protocol::encode_snapshot(response_, {
		.device_id = device_id_,
		.boot_epoch = boot_epoch_,
		.uptime_seconds = seconds,
		.oldest_sample_id = summary.oldest_sample_id,
		.last_sample_id = summary.newest_sample_id,
		.record_count = static_cast<std::uint32_t>(summary.record_count),
		.overwritten_count = summary.overwritten_count
	});
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_journal_service::handle_read_page(sequence_t after_sample_id)
{
	if (after_sample_id != read_after_sample_id_)
	{
		return telemetry_history_protocol::error_response(telemetry_history_error::invalid_cursor);
	}
	{
		journal_lock lock{lock_};
		const auto records_overwritten_during_sync = journal_.summary().overwritten_count != snapshot_overwritten_count_;
		if (records_overwritten_during_sync)
		{
			return telemetry_history_protocol::error_response(telemetry_history_error::records_overwritten);
		}
		const auto page = journal_.read(after_sample_id, snapshot_last_sample_id_);
		if (!page)
		{
			return telemetry_history_protocol::error_response(telemetry_history_error::storage_unavailable);
		}
		if (page->count == 0)
		{
			return telemetry_history_protocol::error_response(telemetry_history_error::records_overwritten);
		}
		page_ = *page;
	}
	sending_page_ = true;
	page_transmitted_ = false;
	response_index_ = 0;
	return {};
}

//////////////////////////////////////////////////////////////////////////

std::string_view telemetry_journal_service::handle_history_ack(sequence_t sample_id)
{
	const auto confirms_transmitted_page = page_transmitted_ && sample_id == page_.last_sample_id();
	if (!confirms_transmitted_page || !acknowledge(sample_id))
	{
		return telemetry_history_protocol::error_response(telemetry_history_error::invalid_acknowledgement);
	}
	read_after_sample_id_ = sample_id;
	return telemetry_history_protocol::encode_acknowledgement(response_, sample_id);
}
