#pragma once
#include "board/board.h"
#include "drivers/uart.h"
#include "drivers/devices/bmp280.h"

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

private:
	board lifecycle_;
	uart console_;
	i2c_bus sensor_bus_;
	i2c_device sensor_transport_;
	bmp280 environment_sensor_;
};
