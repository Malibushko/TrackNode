#include "infrastructure/progress_policy.h"

#include <cassert>
#include <cstdint>

int main()
{
	const progress_observation healthy{
	    service_health::ready, service_health::ready, 9000, 9000
	};
	assert(should_refresh_watchdog(10000, 0, healthy));
	assert(!should_refresh_watchdog(19001, 0, healthy));

	const progress_observation no_card{
	    service_health::ready, service_health::degraded, 9000, 9500
	};
	assert(should_refresh_watchdog(10000, 0, no_card));
	assert(!should_refresh_watchdog(19501, 0, no_card));

	const progress_observation mounting{
	    service_health::ready, service_health::starting, 9000, 0
	};
	assert(should_refresh_watchdog(14999, 0, mounting));
	assert(!should_refresh_watchdog(15000, 0, mounting));

	const progress_observation failed{
	    service_health::ready, service_health::failed, 9000, 9000
	};
	assert(!should_refresh_watchdog(10000, 0, failed));

	const progress_observation wrapped{
	    service_health::ready, service_health::degraded,
	    UINT32_MAX - 100, UINT32_MAX - 100
	};
	assert(should_refresh_watchdog(100, UINT32_MAX - 200, wrapped));
}
