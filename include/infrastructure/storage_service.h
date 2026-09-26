#pragma once

#include "application/log_record.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/service_status.h"
#include "infrastructure/supervisor.h"
#include "drivers/devices/sd_card.h"
#include "drivers/filesystem.h"

#include <atomic>

class storage_service
{
public:
	struct file_config
	{
		const char *directory;
		const char *header;
	};

	storage_service(sd_card &card, file_config file, logger_service &logger, supervisor &monitor)
	    : card_(card), file_(file), logger_(logger), monitor_(monitor) {}

public: // Common lifecycle
	status start();
	void stop();

public: // Storage API
	status submit_record(const log_record &record);
	status request_format();
	[[nodiscard]] storage_snapshot snapshot() const;

private: // Common task loop
	static void task_entry(void *argument);
	static void format_progress(void *argument);
	void run();

private: // Storage internals
	bool run(fatfs::Volume &volume);
	void format_card(fatfs::Volume &volume);
	void wait_for_retry();
	void set_snapshot(service_health health, std::uint64_t size, TickType_t now,
	                  const char *file_name = nullptr);

private: // State
	sd_card &card_;
	file_config file_;
	logger_service &logger_;
	supervisor &monitor_;
	QueueHandle_t inbox_ = nullptr;
	TaskHandle_t task_ = nullptr;
	mutable storage_snapshot snapshot_{};
	std::atomic<std::uint32_t> dropped_{ 0 };
	std::atomic<bool> format_requested_{ false };
};
