#pragma once
#include <drivers/devices/neo6m.h>

#include "logger_service.h"
#include "libs/freertos/freertos_helpers.h"

struct gnss_service_config
{
	uint32_t poll_frequency_ms = 100;
};

class gnss_service
{
public:
	explicit gnss_service(neo6m &device, logger_service& logger, supervisor& monitor);

public: // Common lifecycle
	status start();
	void stop();
	status init();

public: // GNSS API

	using snapshot_t = neo6m::snapshot;

	result<snapshot_t> read();
	bool has_updates() const;

private: // Common private lifecycle

	static void task_entry(void *argument);
	void run();

private: // State

	neo6m &device_;
	gnss_service_config config_;
	logger_service& logger_service_;
	supervisor& monitor_;
	std::optional<snapshot_t> last_snapshot_;

	TaskHandle_t task_ = nullptr;
	bool has_updates_;

	bool initialized_ = false;
};