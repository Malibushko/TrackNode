#pragma once

#include "application/node_controller.h"
#include "infrastructure/console_service.h"
#include "infrastructure/indicator_service.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/sensor_service.h"
#include "infrastructure/storage_service.h"
#include "infrastructure/supervisor.h"
#include "core/component_list.h"
#include "core/hardware_object.h"
#include "drivers/devices/sx126x.h"
#include "infrastructure/gnss_service.h"
#include "infrastructure/radio_service.h"

class track_node : public hardware_object
{
public: // Application lifecycle
	track_node(component_list &devices, uart &console, bmp280 &sensor, sd_card &storage, neo6m& gps, sx126x_driver& radio, watchdog &watchdog_device);
	status init();
	[[noreturn]] void launch();

public: // Application state and diagnostics
	[[nodiscard]] program_state state() const { return supervisor_.state(); }
	[[nodiscard]] std::optional<error> last_error() const { return supervisor_.last_error(); }
	[[nodiscard]] bool can_signal_error() const { return indicator_.diagnostic_ready(); }

	struct delivery_statistics
	{
		std::uint32_t rejected_commands;
		std::uint32_t dropped_logs;
		std::uint32_t failed_uart_writes;
		std::uint32_t dropped_samples;
		std::uint32_t dropped_radio_samples;
	};

	[[nodiscard]] delivery_statistics statistics() const;

private:
	static void init_task(void *argument);
	status startup();
	void stop_workers();

	component_list &devices_;
	supervisor supervisor_;
	logger_service logger_;
	storage_service storage_;
	sensor_service sensor_;
	indicator_service indicator_;
	gnss_service gnss_;
	radio_service radio_;
	node_controller controller_;
	console_service console_;
	bool attempted_ = false;
};
