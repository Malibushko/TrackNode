#pragma once

#include "infrastructure/service_status.h"
#include "drivers/watchdog.h"
#include "libs/freertos/freertos_helpers.h"
#include "platform/error.h"

#include <array>
#include <atomic>
#include <optional>

class supervisor
{
public:
	explicit supervisor(watchdog &device) : watchdog_(device) {}

public: // Common lifecycle
	status start();
	void stop();

public: // Supervisor API
	void configure(std::uint32_t enabled_mask);
	void set_health(service_id id, service_health health);
	void report(service_id id, error failure, service_health health);
	void report_startup(error failure);
	void progress(service_id id, TickType_t now);

	[[nodiscard]] service_health health(service_id id) const;
	[[nodiscard]] service_diagnostic diagnostic(service_id id) const;
	[[nodiscard]] program_state state() const { return state_.load(std::memory_order_relaxed); }
	[[nodiscard]] std::optional<error> last_error() const;

private: // Common task loop
	static void task_entry(void *argument);
	void run();

private: // Supervisor internals
	void recompute_state();

	static constexpr std::size_t service_count = static_cast<std::size_t>(service_id::count);
	static constexpr std::size_t index(service_id id) { return static_cast<std::size_t>(id); }

private: // State
	watchdog &watchdog_;
	TaskHandle_t task_ = nullptr;
	std::array<std::atomic<service_health>, service_count> health_{};
	std::array<std::atomic<TickType_t>, service_count> last_progress_{};
	std::atomic<program_state> state_{ program_state::initialization_error };
	std::optional<error> last_error_;
	std::array<std::optional<error>, service_count> service_errors_{};
};
