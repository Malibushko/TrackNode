#pragma once

#include "app/status_indicator.h"
#include "board/track_node_board.h"
#include "board/track_node_control_protocol.h"
#include "drivers/devices/bmp280.h"
#include "misc/uart_read_adapter.h"
#include "core/hardware_object.h"
#include "platform/error.h"
#include <optional>

class track_node : public hardware_object
{
public:
	explicit track_node(track_node_board &hardware);
	status init();

	[[nodiscard]] program_state state() const
	{
		return state_;
	}

	[[nodiscard]] const std::optional<error> &last_error() const
	{
		return last_error_;
	}

	[[nodiscard]] bool can_signal_error() const
	{
		return diagnostic_ready_;
	}

	[[noreturn]] void launch();

private:

	struct log_message {
		char text[64];
	};

private:
	status startup();

	status init_buffers();
	status init_indicators_tasks();
	status init_protocol_tasks();
	status init_sensor_tasks();
	status init_logger_tasks();

	void update_indicators(uint32_t now);

	void log(const char * fmt, ...);

private:
	void set_error(error failure, bool during_init);

	track_node_board &hardware_;
	gpio diagnostic_led_;
	status_indicator indicator_;
	uart_read_adapter console_input_;
	track_node_control_protocol<uart_read_adapter> protocol_;
	program_state state_ = program_state::initialization_error;
	std::optional<error> last_error_;

	QueueHandle_t indicator_queue_;
	QueueHandle_t sensor_queue_;
	QueueHandle_t logger_queue_;

	bool attempted_ = false;
	bool diagnostic_ready_ = false;
};
