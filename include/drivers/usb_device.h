#pragma once

#include "drivers/gpio.h"
#include "usbd_core.h"

#include <array>
#include <cstdint>

struct usb_device_config
{
	std::uint16_t vendor_id = 0;
	std::uint16_t product_id = 0;
	const char *manufacturer = "TrackNode";
	const char *product = "TrackNode";
	const char *serial = nullptr;
	std::uint32_t irq_priority = 5;
};

struct usb_class_identity
{
	std::uint8_t device_class = 0;
	std::uint8_t subclass = 0;
	std::uint8_t protocol = 0;
};

class usb_device final : public hardware_object
{
public:
	explicit usb_device(usb_device_config config) : config_(config) {}
	usb_device(usb_device &&) = delete;
	usb_device &operator=(usb_device &&) = delete;

public:
	status init();
	status start();
	void stop();
	void deinit();
	status set_vbus_present(bool present);
	[[nodiscard]] bool configured() const;

public:
	status register_function(USBD_ClassTypeDef &driver, void *interface, usb_class_identity identity);
	[[nodiscard]] USBD_HandleTypeDef &native_handle() { return stack_; }
	[[nodiscard]] PCD_HandleTypeDef &pcd_handle() { return pcd_; }
	static usb_device *active() { return active_; }

private:
	static std::uint8_t *device_descriptor(USBD_SpeedTypeDef, std::uint16_t *length);
	static std::uint8_t *language_descriptor(USBD_SpeedTypeDef, std::uint16_t *length);
	static std::uint8_t *manufacturer_descriptor(USBD_SpeedTypeDef, std::uint16_t *length);
	static std::uint8_t *product_descriptor(USBD_SpeedTypeDef, std::uint16_t *length);
	static std::uint8_t *serial_descriptor(USBD_SpeedTypeDef, std::uint16_t *length);
	static std::uint8_t *configuration_descriptor(USBD_SpeedTypeDef, std::uint16_t *length);
	static std::uint8_t *interface_descriptor(USBD_SpeedTypeDef, std::uint16_t *length);
	static std::uint8_t *string_descriptor(const char *text, std::uint16_t *length);
	void prepare_descriptors();

private:
	static usb_device *active_;
	static USBD_DescriptorsTypeDef descriptors_;
	usb_device_config config_;
	USBD_HandleTypeDef stack_{};
	PCD_HandleTypeDef pcd_{};
	alignas(4) std::array<std::uint8_t, 18> device_descriptor_{};
	alignas(4) std::array<std::uint8_t, USBD_MAX_STR_DESC_SIZ> string_descriptor_{};
	std::array<char, 25> serial_{};
	bool initialized_ = false;
	bool started_ = false;
	bool attached_ = false;
	bool vbus_present_ = false;
	bool function_registered_ = false;
};
