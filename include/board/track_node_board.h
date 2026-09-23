#pragma once
#include "board/board.h"
#include "drivers/spi_bus.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "drivers/devices/bmp280.h"
#include "drivers/devices/sd_card.h"

class track_node_board
{
public:
	track_node_board();
	track_node_board(const track_node_board &) = delete;
	track_node_board &operator=(const track_node_board &) = delete;
	track_node_board(track_node_board &&) = delete;
	track_node_board &operator=(track_node_board &&) = delete;

	status init();

	uart &console()
	{
		return console_;
	}

	bmp280 &environment_sensor()
	{
		return environment_sensor_;
	}

	sd_card &storage()
	{
		return sd_card_;
	}

	watchdog & get_watchdog()
	{
		return watchdog_;
	}

private:
	board lifecycle_;
	uart console_;
	i2c_bus sensor_bus_;
	i2c_device sensor_transport_;
	bmp280 environment_sensor_;

	spi_bus spi_bus_;
	sd_card sd_card_;

	watchdog watchdog_;
};
