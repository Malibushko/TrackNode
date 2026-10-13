#pragma once

#include "drivers/usb_device.h"
#include "usbd_cdc.h"

#include <array>
#include <cstddef>
#include <cstdint>

class usb_cdc final : public hardware_object
{
public:
	explicit usb_cdc(usb_device &device) : device_(device) {}
	usb_cdc(usb_cdc &&) = delete;
	usb_cdc &operator=(usb_cdc &&) = delete;

public:
	status init();
	void deinit();

	status start();
	void stop();
	status poll();
	[[nodiscard]] bool connected() const;

public:
	result<std::size_t> read(std::uint8_t *data, std::size_t size);
	result<std::size_t> write(const std::uint8_t *data, std::size_t size, TaskHandle_t waiter = nullptr);
	void clear_write_waiter(TaskHandle_t waiter);
	[[nodiscard]] std::size_t available() const;
	[[nodiscard]] bool tx_busy() const;
	[[nodiscard]] std::uint32_t session() const;

private:
	static std::int8_t on_configured();
	static std::int8_t on_deconfigured();
	static std::int8_t on_control(std::uint8_t command, std::uint8_t *data, std::uint16_t size);
	static std::int8_t on_receive(std::uint8_t *data, std::uint32_t *size);
	static std::int8_t on_transmitted(std::uint8_t *, std::uint32_t *, std::uint8_t);
	void reset_session();
	void notify_write_waiter();
	void arm_receive();

private:
	static usb_cdc *active_;
	static USBD_CDC_ItfTypeDef callbacks_;
	static constexpr std::size_t rx_capacity = 1024;
	static constexpr std::size_t tx_capacity = 256;
	usb_device &device_;
	alignas(4) std::array<std::uint8_t, CDC_DATA_FS_MAX_PACKET_SIZE> rx_packet_{};
	alignas(4) std::array<std::uint8_t, tx_capacity> tx_packet_{};
	std::array<std::uint8_t, rx_capacity> rx_buffer_{};
	std::array<std::uint8_t, 7> line_coding_{0x00, 0xc2, 0x01, 0x00, 0x00, 0x00, 0x08};
	std::size_t rx_head_ = 0;
	std::size_t rx_size_ = 0;
	std::uint32_t session_ = 0;
	bool initialized_ = false;
	bool configured_ = false;
	bool rx_armed_ = false;
	bool tx_busy_ = false;
	TaskHandle_t write_waiter_ = nullptr;
};
