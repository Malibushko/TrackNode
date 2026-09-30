#pragma once
#include <ctime>
#include <optional>

#include "drivers/uart.h"
#include "drivers/protocols/nmea0183.h"

//
// NEO-6u-blox 6 GPS Module
// https://content.u-blox.com/sites/default/files/products/documents/NEO-6_DataSheet_%28GPS.G6-HW-09005%29.pdf

class neo6m
{
public:

	struct snapshot
	{
		uint64_t sequence = 0;
		double latitude = 0;
		double longitude = 0;
		float speed_mps = 0;
		std::time_t utc_time = 0;
		bool position_valid = false;
		bool speed_valid = false;
		bool time_valid = false;
		TickType_t received_at = 0;
	};

public:
	neo6m(uart& transport) : transport_(transport)
	{

	}

	status init();
	status standby_for(uint32_t time_ms);
	status wake();
	[[nodiscard]] result<snapshot> read_snapshot(uint32_t timeout_ms);

private:
	status update_snapshot(uint32_t timeout_ms);

private:
	uart& transport_;
	nmea_protocol protocol_;

	std::optional<snapshot> last_snapshot_;

	bool initialized_{false};
};
