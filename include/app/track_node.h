#pragma once

#include "app/status_indicator.h"
#include "board/track_node_board.h"
#include "board/track_node_control_protocol.h"
#include "drivers/devices/bmp280.h"
#include "misc/uart_read_adapter.h"
#include "core/hardware_object.h"
#include "platform/error.h"
#include <optional>
#include <atomic>

class track_node : public hardware_object
{
public:
	explicit track_node(track_node_board &hardware);
	status init();

	[[nodiscard]] program_state state() const
	{
		return state_.load(std::memory_order_relaxed);
	}

	[[nodiscard]] std::optional<error> last_error() const
	{
		const auto mask = __get_PRIMASK();
		__disable_irq();
		auto value = last_error_;
		__set_PRIMASK(mask);
		return value;
	}

	struct delivery_statistics
	{
		uint32_t dropped_commands;
		uint32_t dropped_logs;
		uint32_t failed_uart_writes;
	};

	[[nodiscard]] delivery_statistics statistics() const
	{
		return { dropped_commands_.load(), dropped_logs_.load(), failed_uart_writes_.load() };
	}

	[[nodiscard]] bool can_signal_error() const
	{
		return diagnostic_ready_;
	}

	[[noreturn]] void launch();

private:
	struct sensor_record
	{
		TickType_t timestamp;
		float temperature;
		float pressure;
	};

private:
	struct log_message
	{
		char text[64];
	};

private:
	status startup();

	status init_buffers();
	status init_indicators_tasks();
	status init_protocol_tasks();
	status init_sensor_tasks();
	status init_logger_tasks();
	status init_storage_tasks();

	void update_indicators(uint32_t now);

	void log(const char *fmt, ...);

private:
	void set_error(error failure, bool during_init);

	track_node_board &hardware_;
	gpio diagnostic_led_;
	status_indicator indicator_;
	uart_read_adapter console_input_;
	track_node_control_protocol<uart_read_adapter> protocol_;
	std::atomic<program_state> state_{ program_state::initialization_error };
	std::optional<error> last_error_;

	QueueHandle_t indicator_queue_{};
	QueueHandle_t sensor_queue_{};
	QueueHandle_t logger_queue_{};
	QueueHandle_t storage_queue_{};
	TaskHandle_t worker_tasks_[5]{};

	bool attempted_ = false;
	std::atomic<bool> diagnostic_ready_{ false };
	std::atomic<uint32_t> dropped_commands_{ 0 };
	std::atomic<uint32_t> dropped_logs_{ 0 };
	std::atomic<uint32_t> failed_uart_writes_{ 0 };
};
