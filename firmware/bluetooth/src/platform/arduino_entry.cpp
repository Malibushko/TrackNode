#include "application/firmware_application.h"
#include "composition/firmware.h"
#include <Arduino.h>

void setup()
{
	const auto started = ble::firmware().start();
	if (!started)
	{
		return;
	}
}

void loop()
{
	ble::firmware().poll();
	delay(1);
}
