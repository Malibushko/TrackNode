#pragma once
#include <drivers/devices/neo6m.h>

#include "logger_service.h"
#include "common/power_mode.h"
#include "infrastructure/gnss_lookup_strategy.h"
#include "libs/freertos/freertos_helpers.h"

struct gnss_service_config
{
	uint32_t poll_timeout_ms = 5000;
	uint32_t sleep_check_ms = 1000;
};

class gnss_service
{
public:
	explicit gnss_service(neo6m &device, logger_service& logger, supervisor& monitor);

public: // Common lifecycle
	status start();
	void set_power_mode(power_mode mode);
	void request_lookup();
	void stop();
	status init();

public: // GNSS API

	using snapshot_t = neo6m::snapshot;

	result<snapshot_t> read();
	[[nodiscard]] bool has_updates() const;

private: // Common private lifecycle

	static void task_entry(void *argument);
	void run();

private: // State

	neo6m &device_;
	gnss_service_config config_;
	logger_service& logger_service_;
	supervisor& monitor_;
	std::optional<snapshot_t> last_snapshot_;
	power_mode power_mode_{ power_mode::high };
	gnss_lookup_strategy lookup_;
	bool lookup_requested_ = false;

	TaskHandle_t task_ = nullptr;
	bool has_updates_;
	bool initialized_ = false;
};
