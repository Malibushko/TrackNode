#include "infrastructure/storage_service.h"

#include "common/common.h"
#include "fault/fault_injection.h"
#include "libs/fatfs/sd_diskio.h"

#include "perf.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
constexpr std::uint32_t max_session_id = 99999999;

bool parse_session_id(const char *name, std::uint32_t &id)
{
	if (std::strlen(name) != 12 || name[8] != '.' ||
	    (name[9] | 0x20) != 'c' || (name[10] | 0x20) != 's' ||
	    (name[11] | 0x20) != 'v')
	{
		return false;
	}

	std::uint32_t parsed = 0;
	for (int index = 0; index < 8; ++index)
	{
		if (name[index] < '0' || name[index] > '9')
		{
			return false;
		}
		parsed = parsed * 10 + static_cast<std::uint32_t>(name[index] - '0');
	}
	id = parsed;
	return true;
}

//////////////////////////////////////////////////////////////////////////

FRESULT next_session_id(const char *path, std::uint32_t &id)
{
	fatfs::Dir directory;
	FRESULT result = directory.open(path);
	if (result != FR_OK)
	{
		return result;
	}

	std::uint32_t highest = 0;
	result = directory.for_each([&highest](const FILINFO &entry)
	{
		std::uint32_t existing = 0;
		if (!fatfs::Dir::is_dir(entry) && parse_session_id(entry.fname, existing) &&
		    existing > highest)
		{
			highest = existing;
		}
	});
	if (result != FR_OK)
	{
		return result;
	}
	if (highest == max_session_id)
	{
		return FR_DENIED;
	}
	id = highest + 1;
	return FR_OK;
}

//////////////////////////////////////////////////////////////////////////
}

status storage_service::start()
{
	if (!file_.directory || !file_.header)
	{
		return fail(component::storage, error_code::invalid_config);
	}
	if (inbox_)
	{
		return fail(component::rtos, error_code::already_initialized);
	}
	inbox_ = xQueueCreate(4, sizeof(log_record));
	if (!inbox_)
	{
		return fail(component::rtos, error_code::out_of_memory);
	}

	auto created = task_create(task_entry, "storage", configMINIMAL_STACK_SIZE * 8,
	                           this, tskIDLE_PRIORITY);
	if (!created)
	{
		vQueueDelete(inbox_);
		inbox_ = nullptr;
		return tl::make_unexpected(created.error());
	}
	task_ = *created;
	return {};
}

//////////////////////////////////////////////////////////////////////////

void storage_service::stop()
{
	format_requested_.store(false);
	set_sd_disk_progress_callback(nullptr, nullptr);
	if (task_)
	{
		vTaskDelete(task_);
		task_ = nullptr;
	}
	if (inbox_)
	{
		vQueueDelete(inbox_);
		inbox_ = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

status storage_service::submit_record(const log_record &record)
{
	if (record.length == 0 || record.length > record.text.size())
	{
		dropped_.fetch_add(1);
		return fail(component::storage, error_code::invalid_argument);
	}
	if (!inbox_ || format_requested_.load() ||
	    monitor_.health(service_id::storage) != service_health::ready)
	{
		dropped_.fetch_add(1);
		return fail(component::storage, error_code::not_initialized);
	}

	if (xQueueSend(inbox_, &record, 0) != pdPASS)
	{
		dropped_.fetch_add(1);
		return fail(component::storage, error_code::queue_full);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status storage_service::request_format()
{
	if (!task_)
	{
		return fail(component::storage, error_code::not_initialized);
	}
	bool expected = false;
	if (!format_requested_.compare_exchange_strong(expected, true))
	{
		return fail(component::storage, error_code::already_initialized);
	}
	xTaskNotifyGive(task_);
	return {};
}

//////////////////////////////////////////////////////////////////////////

storage_snapshot storage_service::snapshot() const
{
	taskENTER_CRITICAL();
	auto value = snapshot_;
	taskEXIT_CRITICAL();
	value.dropped_samples = dropped_.load(std::memory_order_relaxed);
	return value;
}

//////////////////////////////////////////////////////////////////////////

void storage_service::set_snapshot(service_health health, std::uint64_t size, TickType_t now,
	                               const char *file_name)
{
	taskENTER_CRITICAL();
	snapshot_.health = health;
	snapshot_.file_name.fill('\0');
	if (file_name)
	{
		std::strncpy(snapshot_.file_name.data(), file_name, snapshot_.file_name.size() - 1);
	}
	snapshot_.log_size = size;
	snapshot_.observed_at = now;
	taskEXIT_CRITICAL();
}

//////////////////////////////////////////////////////////////////////////

void storage_service::task_entry(void *argument)
{
	static_cast<storage_service *>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void storage_service::format_progress(void *argument)
{
	auto &service = *static_cast<storage_service *>(argument);
	service.monitor_.progress(service_id::storage, common::get_tick());
}

//////////////////////////////////////////////////////////////////////////

void storage_service::wait_for_retry()
{
	(void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
}

//////////////////////////////////////////////////////////////////////////

void storage_service::format_card(fatfs::Volume &volume)
{
	set_snapshot(service_health::degraded, 0, common::get_tick());
	monitor_.set_health(service_id::storage, service_health::degraded);
	dropped_.fetch_add(uxQueueMessagesWaiting(inbox_), std::memory_order_relaxed);

	xQueueReset(inbox_);
	monitor_.progress(service_id::storage, common::get_tick());
	(void)logger_.write("Storage format started; all SD data will be erased\r\n");

	static std::array<BYTE, 4096> work{};
	set_sd_disk_progress_callback(format_progress, this);
	FRESULT result = volume.mkfs(fatfs::MkfsOptions{}, work.data(), work.size());
	set_sd_disk_progress_callback(nullptr, nullptr);
	if (result == FR_OK)
	{
		result = volume.mount();
	}
	volume.unmount();
	format_requested_.store(false);
	monitor_.progress(service_id::storage, common::get_tick());

	if (result == FR_OK)
	{
		(void)logger_.write("OK STORAGE_FORMAT; filesystem verified\r\n");
	}
	else
	{
		(void)logger_.write("ERR STORAGE_FORMAT: %u\r\n", static_cast<unsigned>(result));
	}
}

//////////////////////////////////////////////////////////////////////////

void storage_service::run()
{
	fatfs::Volume volume(card_);
	for (;;)
	{
		if (auto initialized = card_.init(); !initialized)
		{
			(void)logger_.write("SD card init failed: %u\r\n",
			                    static_cast<unsigned>(initialized.error().code));
			set_snapshot(service_health::degraded, 0, common::get_tick());
			monitor_.report(service_id::storage, initialized.error(), service_health::degraded);
			monitor_.progress(service_id::storage, common::get_tick());
			wait_for_retry();
			continue;
		}

		if (format_requested_.load())
		{
			format_card(volume);
			continue;
		}

		const FRESULT mounted = volume.mount();
		if (mounted == FR_OK)
		{
			run(volume);
		}
		else
		{
			(void)logger_.write("Storage mount failed: %u\r\n", static_cast<unsigned>(mounted));
		}

		set_snapshot(service_health::degraded, 0, common::get_tick());
		if (format_requested_.load())
		{
			monitor_.set_health(service_id::storage, service_health::degraded);
		}
		else
		{
			monitor_.report(service_id::storage,
			                error(component::storage, error_code::storage_error),
			                service_health::degraded);
		}
		monitor_.progress(service_id::storage, common::get_tick());
		dropped_.fetch_add(uxQueueMessagesWaiting(inbox_), std::memory_order_relaxed);
		xQueueReset(inbox_);
		volume.unmount();
		if (format_requested_.load())
		{
			format_card(volume);
			continue;
		}

		// Keep the sensor and console alive. A later card insertion can recover.
		wait_for_retry();
	}
}

//////////////////////////////////////////////////////////////////////////

bool storage_service::run(fatfs::Volume &volume)
{
	(void)volume;
	FRESULT result = fatfs::Volume::mkdir(file_.directory);
	if (result != FR_OK && result != FR_EXIST)
	{
		(void)logger_.write("Unable to create logs directory: %u\r\n", static_cast<unsigned>(result));
		return false;
	}

	std::uint32_t session_id = 0;
	result = next_session_id(file_.directory, session_id);
	if (result != FR_OK)
	{
		(void)logger_.write("Unable to find next log session: %u\r\n", static_cast<unsigned>(result));
		return false;
	}

	fatfs::File log;
	char file_name[13]{};
	char path[64]{};
	for (;;)
	{
		const int name_length = std::snprintf(file_name, sizeof(file_name), "%08lu.csv",
		                                      static_cast<unsigned long>(session_id));
		const int path_length = std::snprintf(path, sizeof(path), "%s/%s",
		                                      file_.directory, file_name);
		if (name_length != 12 || path_length < 0 || path_length >= static_cast<int>(sizeof(path)))
		{
			(void)logger_.write("Log path is too long\r\n");
			return false;
		}
		result = log.open(path, FA_WRITE | FA_CREATE_NEW);
		if (result != FR_EXIST || session_id == max_session_id)
		{
			break;
		}
		++session_id;
	}
	if (result != FR_OK)
	{
		(void)logger_.write("Unable to open log file: %u\r\n", static_cast<unsigned>(result));
		return false;
	}

	result = log.write_string(file_.header);
	if (result == FR_OK)
	{
		result = log.sync();
	}
	if (result != FR_OK)
	{
		(void)logger_.write("Unable to initialize log file: %u\r\n", static_cast<unsigned>(result));
		return false;
	}

	set_snapshot(service_health::ready, log.size(), common::get_tick(), file_name);
	monitor_.set_health(service_id::storage, service_health::ready);
	monitor_.progress(service_id::storage, common::get_tick());
	(void)logger_.write("Storage ready: %s\r\n", path);

	log_record record{};
	constexpr TickType_t sync_interval = pdMS_TO_TICKS(5000);
	TickType_t last_sync = common::get_tick();
	bool dirty = false;
	for (;;)
	{
		if (format_requested_.load())
		{
			return true;
		}
		if (xQueueReceive(inbox_, &record, pdMS_TO_TICKS(100)) == pdPASS)
		{
			if (format_requested_.load())
			{
				dropped_.fetch_add(1, std::memory_order_relaxed);
				return true;
			}
			PERF_SCOPE(logger_, "storage record");

			result = log.write(record.text.data(), record.length);
#ifdef FAULT_INJECTION_ENABLE
			fault::task_inject_random_delay(1000, 5000);
#endif
			if (result != FR_OK)
			{
				(void)logger_.write("Unable to write log: %u\r\n", static_cast<unsigned>(result));
				return false;
			}
			dirty = true;
			set_snapshot(service_health::ready, log.size(), common::get_tick(), file_name);
		}

		const TickType_t now = common::get_tick();
		if (dirty && now - last_sync >= sync_interval)
		{
			PERF_SCOPE(logger_, "log sync");

			result = log.sync();
#ifdef FAULT_INJECTION_ENABLE
			fault::task_inject_random_delay(0, 1000);
#endif
			if (result != FR_OK)
			{
				(void)logger_.write("Unable to sync log: %u\r\n", static_cast<unsigned>(result));
				return false;
			}
			last_sync = now;
			dirty = false;
		}
		monitor_.progress(service_id::storage, now);
	}
}

//////////////////////////////////////////////////////////////////////////
