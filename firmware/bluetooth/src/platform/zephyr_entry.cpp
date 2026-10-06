#include "application/firmware_application.h"
#include "composition/firmware.h"
#include <zephyr/kernel.h>

int main()
{
	auto &application = ble::firmware();
	const auto started = application.start();
	if (!started)
	{
		return 1;
	}

	while (true)
	{
		const auto polled = application.poll();
		if (!polled)
		{
			application.stop();
			return 1;
		}

		k_msleep(1);
	}
}
