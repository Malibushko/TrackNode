#include "infrastructure/storage_service.h"

#include "common/common.h"
#include "fault/fault_injection.h"
#include "libs/fatfs/sd_diskio.h"
#include "fault/fault_injection.h"

#include "perf.h"

status storage_service::start()
{
	if (!file_.directory || !file_.path || !file_.header)
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
	if (!inbox_ || monitor_.health(service_id::storage) != service_health::ready)
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

storage_snapshot storage_service::snapshot() const
{
	taskENTER_CRITICAL();
	auto value = snapshot_;
	taskEXIT_CRITICAL();
	value.dropped_samples = dropped_.load(std::memory_order_relaxed);
	return value;
}

//////////////////////////////////////////////////////////////////////////

void storage_service::set_snapshot(service_health health, std::uint64_t size, TickType_t now)
{
	taskENTER_CRITICAL();
	snapshot_.health = health;
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
			vTaskDelay(pdMS_TO_TICKS(5000));
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
		monitor_.report(service_id::storage,
		                error(component::storage, error_code::storage_error),
		                service_health::degraded);
		monitor_.progress(service_id::storage, common::get_tick());
		dropped_.fetch_add(uxQueueMessagesWaiting(inbox_), std::memory_order_relaxed);
		xQueueReset(inbox_);
		volume.unmount();

		// Keep the sensor and console alive. A later card insertion can recover.
		vTaskDelay(pdMS_TO_TICKS(5000));
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

	fatfs::File log;
	result = log.open(file_.path, FA_WRITE | FA_OPEN_APPEND);
	if (result != FR_OK)
	{
		(void)logger_.write("Unable to open log file: %u\r\n", static_cast<unsigned>(result));
		return false;
	}

	if (log.size() == 0)
	{
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
	}

	set_snapshot(service_health::ready, log.size(), common::get_tick());
	monitor_.set_health(service_id::storage, service_health::ready);
	monitor_.progress(service_id::storage, common::get_tick());
	(void)logger_.write("Storage ready\r\n");

	log_record record{};
	constexpr TickType_t sync_interval = pdMS_TO_TICKS(5000);
	TickType_t last_sync = common::get_tick();
	bool dirty = false;
	for (;;)
	{
		if (xQueueReceive(inbox_, &record, sync_interval) == pdPASS)
		{
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
			set_snapshot(service_health::ready, log.size(), common::get_tick());
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
