#pragma once

#include "drivers/bluetooth_transport.h"

class uart;

class xiao_ble_bridge final : public bluetooth_transport
{
public:
	explicit xiao_ble_bridge(uart &transport) : transport_(transport) {}
	status init() override;
	void stop() override;
	status send(std::span<const std::uint8_t> payload, std::uint32_t session) override;
	result<bluetooth_event> poll(std::uint32_t timeout_ms) override;

private:
	uart &transport_;
};
