#pragma once

#include "drivers/serial_device.h"
#include "drivers/usb_cdc.h"

#include <optional>

class usb_cdc_serial_device final : public serial_device
{
public:
	explicit usb_cdc_serial_device(usb_cdc &device) : serial_device(component::usb), device_(device) {}
	status init();
	result<std::size_t> read(std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms = 0) override;
	result<std::size_t> write(const std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms = 100) override;

private:
	result<bool> begin_write(const std::uint8_t *data, std::size_t size) override;
	result<std::size_t> finish_write() override;
	void cancel_write() override;

private:
	usb_cdc &device_;
	std::optional<std::uint32_t> read_session_;
	std::uint32_t write_session_ = 0;
	std::size_t write_size_ = 0;
	TaskHandle_t write_waiter_ = nullptr;
	bool write_pending_ = false;
};
