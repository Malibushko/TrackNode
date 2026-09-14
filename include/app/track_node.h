#pragma once

#include "app/status_indicator.h"
#include "board/board.h"
#include "board/track_node_control_protocol.h"
#include "misc/uart_read_adapter.h"
#include <optional>

class track_node : public hardware_object
{
public:
	explicit track_node(board &hardware);
	status init();
	void update(uint32_t now);

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

private:
	void process_command(uint32_t now);
	void set_error(error failure, bool during_init);

	board &hardware_;
	gpio diagnostic_led_;
	status_indicator indicator_;
	uart_read_adapter console_input_;
	track_node_control_protocol<uart_read_adapter> protocol_;
	program_state state_ = program_state::initialization_error;
	std::optional<error> last_error_;
	bool attempted_ = false;
	bool diagnostic_ready_ = false;
};
