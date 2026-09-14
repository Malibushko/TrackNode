#pragma once
#include <variant>

//	The BMP280 is a compact digital barometric pressure and temperature sensor designed by Bosch Sensortec
//	for mobile and microcontroller applications
// https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf

class bmp280
{
public:
	using transport_t = std::variant<i2c_device>;

public:
	explicit bmp280(transport_t& transport)
	{

	}

private:
	transport_t transport_;
};