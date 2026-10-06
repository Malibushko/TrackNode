#pragma once

#include "drivers/bluetooth_backend.h"

namespace ble
{
struct bluetooth_config
{
	const char *name = nullptr;
	const char *service_uuid = nullptr;
	const char *rx_uuid = nullptr;
	const char *tx_uuid = nullptr;
};

class bluefruit_backend final : public bluetooth_backend
{
public:
	explicit bluefruit_backend(bluetooth_config config);
	status init() override;
	void stop() override;
	status send(data_view data, session_type session) override;
	result<bluetooth_event> poll(std::uint32_t timeout_ms) override;

private:
	bluetooth_config config_;
};
}
