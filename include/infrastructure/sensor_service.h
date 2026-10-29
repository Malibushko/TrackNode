#pragma once

#include "drivers/devices/bmp280.h"

// Device access only. Sampling cadence and destination belong to node_controller.
class sensor_service
{
public:
	explicit sensor_service(bmp280 &device) : device_(device) {}

public: // Sensor API
	status init();
	result<bmp280::environmental_sample> read();

private: // Measurement
	status convert_once();

private: // State
	bmp280 &device_;
	bool initialized_ = false;
};
