#pragma once

#include "application/telemetry_sink.h"
#include "application/command_sink.h"
#include "drivers/bluetooth_transport.h"

class logger_service;
class supervisor;

// Scaffold: no GATT, UART framing or RTOS worker is implemented here yet.
class bluetooth_service final : public telemetry_sink, public command_reply_sink
{
public:
	bluetooth_service(bluetooth_transport &backend, command_sink &commands,
	                  logger_service &logger, supervisor &monitor)
		: backend_(backend), commands_(commands), logger_(logger), monitor_(monitor) {}

public: // Common lifecycle
	status start();
	void stop();

public: // Application ports
	status submit(const telemetry_sample &sample) override;
	status submit_reply(const command_reply &reply) override;
	void set_power_mode(power_mode mode) override;

private:
	bluetooth_transport &backend_;
	command_sink &commands_;
	logger_service &logger_;
	supervisor &monitor_;
};
