#include "drivers/usb_device.h"

#include <cstdio>
#include <cstring>

usb_device *usb_device::active_ = nullptr;
USBD_DescriptorsTypeDef usb_device::descriptors_{
	.GetDeviceDescriptor = device_descriptor,
	.GetLangIDStrDescriptor = language_descriptor,
	.GetManufacturerStrDescriptor = manufacturer_descriptor,
	.GetProductStrDescriptor = product_descriptor,
	.GetSerialStrDescriptor = serial_descriptor,
	.GetConfigurationStrDescriptor = configuration_descriptor,
	.GetInterfaceStrDescriptor = interface_descriptor
};

//////////////////////////////////////////////////////////////////////////

status usb_device::init()
{
	if (initialized_)
	{
		return fail(component::usb, error_code::already_initialized);
	}
	const bool valid_identity = config_.vendor_id != 0 && config_.product_id != 0 &&
		config_.manufacturer && config_.product;
	const auto lowest_safe_priority = configMAX_SYSCALL_INTERRUPT_PRIORITY >> (8U - __NVIC_PRIO_BITS);
	const bool valid_irq = config_.irq_priority >= lowest_safe_priority &&
		config_.irq_priority < (1U << __NVIC_PRIO_BITS);
	if (!valid_identity || !valid_irq || (active_ && active_ != this))
	{
		return fail(component::usb, error_code::invalid_config);
	}
	const auto pll = RCC->PLLCFGR;
	const auto pll_m = pll & RCC_PLLCFGR_PLLM;
	const auto pll_n = (pll & RCC_PLLCFGR_PLLN) >> 6U;
	const auto pll_q = (pll & RCC_PLLCFGR_PLLQ) >> 24U;
	const auto source_hz = (pll & RCC_PLLCFGR_PLLSRC) ? HSE_VALUE : HSI_VALUE;
	const bool pll_ready = (RCC->CR & RCC_CR_PLLRDY) != 0;
	const bool usb_clock_valid = pll_m != 0 && pll_q != 0 && pll_ready &&
		static_cast<std::uint64_t>(source_hz) * pll_n == 48'000'000ULL * pll_m * pll_q;
	if (!usb_clock_valid)
	{
		return fail(component::usb, error_code::invalid_config);
	}
	const bool vbus_pin_present = config_.vbus.port != nullptr;
	const bool single_vbus_pin = config_.vbus.mask != 0 &&
		(config_.vbus.mask & (config_.vbus.mask - 1)) == 0;
	const bool vbus_overlaps_data = config_.vbus.port == GPIOA &&
		(config_.vbus.mask & (GPIO_PIN_11 | GPIO_PIN_12)) != 0;
	const bool valid_vbus_pin = vbus_pin_present ? single_vbus_pin : config_.vbus.mask == 0;
	if (!valid_vbus_pin || vbus_overlaps_data)
	{
		return fail(component::usb, error_code::invalid_config);
	}
	if (vbus_pin_present)
	{
		gpio detector{gpio_config{
			.pin = config_.vbus,
			.mode = GPIO_MODE_INPUT,
			.pull = GPIO_NOPULL
		}};
		if (auto initialized = detector.init(); !initialized)
		{
			return initialized;
		}
	}
	prepare_descriptors();
	active_ = this;
	pcd_.Instance = USB_OTG_FS;
	pcd_.Init.dev_endpoints = 4;
	pcd_.Init.speed = PCD_SPEED_FULL;
	pcd_.Init.dma_enable = DISABLE;
	pcd_.Init.phy_itface = PCD_PHY_EMBEDDED;
	pcd_.Init.Sof_enable = DISABLE;
	pcd_.Init.low_power_enable = DISABLE;
	pcd_.Init.lpm_enable = DISABLE;
	pcd_.Init.vbus_sensing_enable = DISABLE;
	pcd_.Init.use_dedicated_ep1 = DISABLE;
	if (USBD_Init(&stack_, &descriptors_, 0) != USBD_OK)
	{
		(void)HAL_PCD_DeInit(&pcd_);
		active_ = nullptr;
		return fail(component::usb, error_code::hal_failure);
	}
	HAL_NVIC_SetPriority(OTG_FS_IRQn, config_.irq_priority, 0);
	initialized_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status usb_device::start()
{
	if (!initialized_)
	{
		return fail(component::usb, error_code::not_initialized);
	}
	if (started_)
	{
		return fail(component::usb, error_code::already_initialized);
	}
	if (!function_registered_)
	{
		return fail(component::usb, error_code::invalid_config);
	}
	started_ = true;
	if (auto attached = config_.vbus.port ? poll() : set_vbus_present(vbus_present_); !attached)
	{
		started_ = false;
		return attached;
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

void usb_device::stop()
{
	if (!initialized_)
	{
		return;
	}
	HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
	if (attached_)
	{
		attached_ = false;
		(void)USBD_Stop(&stack_);
		stack_.dev_state = USBD_STATE_DEFAULT;
		stack_.dev_config = 0;
	}
	attached_ = false;
	started_ = false;
}

//////////////////////////////////////////////////////////////////////////

void usb_device::deinit()
{
	stop();
	if (initialized_)
	{
		(void)USBD_DeInit(&stack_);
	}
	initialized_ = false;
	function_registered_ = false;
	vbus_present_ = false;
	if (active_ == this)
	{
		active_ = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

status usb_device::set_vbus_present(bool present)
{
	if (!initialized_)
	{
		return fail(component::usb, error_code::not_initialized);
	}
	vbus_present_ = present;
	if (!started_ || attached_ == present)
	{
		return {};
	}
	HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
	if (present)
	{
		HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
		if (USBD_Start(&stack_) != USBD_OK)
		{
			(void)USBD_Stop(&stack_);
			return fail(component::usb, error_code::hal_failure);
		}
		attached_ = true;
		HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
	}
	else
	{
		attached_ = false;
		(void)USBD_Stop(&stack_);
		stack_.dev_state = USBD_STATE_DEFAULT;
		stack_.dev_config = 0;
		attached_ = false;
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

status usb_device::poll()
{
	if (!initialized_)
	{
		return fail(component::usb, error_code::not_initialized);
	}
	if (!config_.vbus.port)
	{
		return {};
	}
	return set_vbus_present(HAL_GPIO_ReadPin(config_.vbus.port, config_.vbus.mask) == GPIO_PIN_SET);
}

//////////////////////////////////////////////////////////////////////////

bool usb_device::configured() const
{
	return initialized_ && started_ && attached_ && stack_.dev_state == USBD_STATE_CONFIGURED;
}

//////////////////////////////////////////////////////////////////////////

status usb_device::register_function(USBD_ClassTypeDef &driver, void *interface, usb_class_identity identity)
{
	if (!initialized_)
	{
		return fail(component::usb, error_code::not_initialized);
	}
	if (started_ || function_registered_ || !interface)
	{
		return fail(component::usb, error_code::invalid_config);
	}
	if (USBD_RegisterClass(&stack_, &driver) != USBD_OK)
	{
		return fail(component::usb, error_code::device_error);
	}
	stack_.pUserData[0] = interface;
	device_descriptor_[4] = identity.device_class;
	device_descriptor_[5] = identity.subclass;
	device_descriptor_[6] = identity.protocol;
	function_registered_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

std::uint8_t *usb_device::device_descriptor(USBD_SpeedTypeDef, std::uint16_t *length)
{
	*length = active_->device_descriptor_.size();
	return active_->device_descriptor_.data();
}

//////////////////////////////////////////////////////////////////////////

std::uint8_t *usb_device::language_descriptor(USBD_SpeedTypeDef, std::uint16_t *length)
{
	static std::uint8_t language[]{4, USB_DESC_TYPE_STRING, 0x09, 0x04};
	*length = sizeof(language);
	return language;
}

//////////////////////////////////////////////////////////////////////////

std::uint8_t *usb_device::manufacturer_descriptor(USBD_SpeedTypeDef, std::uint16_t *length)
{
	return string_descriptor(active_->config_.manufacturer, length);
}

//////////////////////////////////////////////////////////////////////////

std::uint8_t *usb_device::product_descriptor(USBD_SpeedTypeDef, std::uint16_t *length)
{
	return string_descriptor(active_->config_.product, length);
}

//////////////////////////////////////////////////////////////////////////

std::uint8_t *usb_device::serial_descriptor(USBD_SpeedTypeDef, std::uint16_t *length)
{
	return string_descriptor(active_->config_.serial ? active_->config_.serial : active_->serial_.data(), length);
}

//////////////////////////////////////////////////////////////////////////

std::uint8_t *usb_device::configuration_descriptor(USBD_SpeedTypeDef, std::uint16_t *length)
{
	return string_descriptor("TrackNode configuration", length);
}

//////////////////////////////////////////////////////////////////////////

std::uint8_t *usb_device::interface_descriptor(USBD_SpeedTypeDef, std::uint16_t *length)
{
	return string_descriptor(active_->config_.product, length);
}

//////////////////////////////////////////////////////////////////////////

std::uint8_t *usb_device::string_descriptor(const char *text, std::uint16_t *length)
{
	auto &buffer = active_->string_descriptor_;
	const auto count = strnlen(text, (buffer.size() - 2) / 2);
	*length = static_cast<std::uint16_t>(2 + count * 2);
	buffer[0] = *length;
	buffer[1] = USB_DESC_TYPE_STRING;
	for (std::size_t i = 0; i < count; ++i)
	{
		buffer[2 + i * 2] = static_cast<std::uint8_t>(text[i]);
		buffer[3 + i * 2] = 0;
	}
	return buffer.data();
}

//////////////////////////////////////////////////////////////////////////

void usb_device::prepare_descriptors()
{
	device_descriptor_ = {18, USB_DESC_TYPE_DEVICE, 0x00, 0x02, 0, 0, 0, 64,
		static_cast<std::uint8_t>(config_.vendor_id), static_cast<std::uint8_t>(config_.vendor_id >> 8),
		static_cast<std::uint8_t>(config_.product_id), static_cast<std::uint8_t>(config_.product_id >> 8),
		0x00, 0x01, 1, 2, 3, 1};
	std::snprintf(serial_.data(), serial_.size(), "%08lX%08lX%08lX",
		static_cast<unsigned long>(HAL_GetUIDw0()),
		static_cast<unsigned long>(HAL_GetUIDw1()),
		static_cast<unsigned long>(HAL_GetUIDw2()));
}

//////////////////////////////////////////////////////////////////////////
