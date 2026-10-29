#include "drivers/usb_cdc.h"

#include "common/task_events.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr std::uint8_t communications_class = 0x02;
constexpr std::uint8_t device_subclass_none = 0x00;
constexpr std::uint8_t device_protocol_none = 0x00;
}

usb_cdc *usb_cdc::active_ = nullptr;
USBD_CDC_ItfTypeDef usb_cdc::callbacks_{
	.Init = on_configured,
	.DeInit = on_deconfigured,
	.Control = on_control,
	.Receive = on_receive,
	.TransmitCplt = on_transmitted
};

//////////////////////////////////////////////////////////////////////////

status usb_cdc::init()
{
	if (initialized_)
	{
		return fail(component::usb, error_code::already_initialized);
	}
	if (active_ && active_ != this)
	{
		return fail(component::usb, error_code::invalid_config);
	}
	const usb_class_identity identity{
		.device_class = communications_class,
		.subclass = device_subclass_none,
		.protocol = device_protocol_none
	};
	if (auto registered = device_.register_function(USBD_CDC, &callbacks_, identity); !registered)
	{
		return registered;
	}
	active_ = this;
	initialized_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status usb_cdc::start()
{
	if (!initialized_)
	{
		return fail(component::usb, error_code::not_initialized);
	}
	return device_.start();
}

//////////////////////////////////////////////////////////////////////////

status usb_cdc::poll()
{
	return device_.poll();
}

//////////////////////////////////////////////////////////////////////////

void usb_cdc::stop()
{
	if (!initialized_)
	{
		return;
	}
	device_.stop();
	taskENTER_CRITICAL();
	reset_session();
	taskEXIT_CRITICAL();
}

//////////////////////////////////////////////////////////////////////////

void usb_cdc::deinit()
{
	if (initialized_)
	{
		stop();
		device_.deinit();
		initialized_ = false;
	}
	if (active_ == this)
	{
		active_ = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

bool usb_cdc::connected() const
{
	taskENTER_CRITICAL();
	const bool ready = configured_ && device_.configured();
	taskEXIT_CRITICAL();
	return ready;
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> usb_cdc::read(std::uint8_t *data, std::size_t size)
{
	if (!initialized_)
	{
		return fail(component::usb, error_code::not_initialized);
	}
	if (!data && size != 0)
	{
		return fail(component::usb, error_code::invalid_argument);
	}
	taskENTER_CRITICAL();
	if (!configured_ || !device_.configured())
	{
		taskEXIT_CRITICAL();
		return fail(component::usb, error_code::not_ready);
	}
	const auto count = std::min(size, rx_size_);
	for (std::size_t i = 0; i < count; ++i)
	{
		data[i] = rx_buffer_[(rx_head_ + i) % rx_capacity];
	}
	rx_head_ = (rx_head_ + count) % rx_capacity;
	rx_size_ -= count;
	arm_receive();
	taskEXIT_CRITICAL();
	return count;
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> usb_cdc::write(const std::uint8_t *data, std::size_t size, TaskHandle_t waiter)
{
	if (!initialized_)
	{
		return fail(component::usb, error_code::not_initialized);
	}
	if (!data && size != 0)
	{
		return fail(component::usb, error_code::invalid_argument);
	}
	taskENTER_CRITICAL();
	if (!configured_ || !device_.configured() || tx_busy_)
	{
		taskEXIT_CRITICAL();
		return fail(component::usb, error_code::not_ready);
	}
	const auto count = std::min(size, tx_capacity);
	if (count == 0)
	{
		taskEXIT_CRITICAL();
		return std::size_t{0};
	}
	std::memcpy(tx_packet_.data(), data, count);
	auto &stack = device_.native_handle();
	USBD_CDC_SetTxBuffer(&stack, tx_packet_.data(), count);
	write_waiter_ = waiter;
	tx_busy_ = true;
	const auto sent = USBD_CDC_TransmitPacket(&stack);
	if (sent != USBD_OK)
	{
		write_waiter_ = nullptr;
		tx_busy_ = false;
		taskEXIT_CRITICAL();
		return fail(component::usb, error_code::not_ready);
	}
	taskEXIT_CRITICAL();
	return count;
}

//////////////////////////////////////////////////////////////////////////

void usb_cdc::set_receive_listener(TaskHandle_t task)
{
	receive_listener_ = task;
}

//////////////////////////////////////////////////////////////////////////

void usb_cdc::clear_write_waiter(TaskHandle_t waiter)
{
	taskENTER_CRITICAL();
	if (write_waiter_ == waiter)
	{
		write_waiter_ = nullptr;
	}
	taskEXIT_CRITICAL();
}

//////////////////////////////////////////////////////////////////////////

std::size_t usb_cdc::available() const
{
	taskENTER_CRITICAL();
	const auto count = rx_size_;
	taskEXIT_CRITICAL();
	return count;
}

//////////////////////////////////////////////////////////////////////////

bool usb_cdc::tx_busy() const
{
	taskENTER_CRITICAL();
	const auto busy = tx_busy_;
	taskEXIT_CRITICAL();
	return busy;
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t usb_cdc::session() const
{
	taskENTER_CRITICAL();
	const auto current = session_;
	taskEXIT_CRITICAL();
	return current;
}

//////////////////////////////////////////////////////////////////////////

std::int8_t usb_cdc::on_configured()
{
	auto &owner = *active_;
	owner.reset_session();
	owner.configured_ = true;
	owner.rx_armed_ = true;
	USBD_CDC_SetRxBuffer(&owner.device_.native_handle(), owner.rx_packet_.data());
	owner.notify_receive_listener();
	return USBD_OK;
}

//////////////////////////////////////////////////////////////////////////

std::int8_t usb_cdc::on_deconfigured()
{
	active_->reset_session();
	active_->notify_receive_listener();
	return USBD_OK;
}

//////////////////////////////////////////////////////////////////////////

std::int8_t usb_cdc::on_control(std::uint8_t command, std::uint8_t *data, std::uint16_t size)
{
	auto &coding = active_->line_coding_;
	if (command == CDC_SET_LINE_CODING || command == CDC_GET_LINE_CODING)
	{
		if (!data || size != coding.size())
		{
			return USBD_FAIL;
		}
		if (command == CDC_SET_LINE_CODING)
		{
			std::memcpy(coding.data(), data, coding.size());
		}
		else
		{
			std::memcpy(data, coding.data(), coding.size());
		}
	}
	return USBD_OK;
}

//////////////////////////////////////////////////////////////////////////

std::int8_t usb_cdc::on_receive(std::uint8_t *data, std::uint32_t *size)
{
	auto &owner = *active_;
	owner.rx_armed_ = false;
	if (!data || !size || *size > owner.rx_packet_.size() || *size > rx_capacity - owner.rx_size_)
	{
		return USBD_FAIL;
	}
	for (std::size_t i = 0; i < *size; ++i)
	{
		owner.rx_buffer_[(owner.rx_head_ + owner.rx_size_ + i) % rx_capacity] = data[i];
	}
	owner.rx_size_ += *size;
	owner.arm_receive();
	owner.notify_receive_listener();
	return USBD_OK;
}

//////////////////////////////////////////////////////////////////////////

std::int8_t usb_cdc::on_transmitted(std::uint8_t *, std::uint32_t *, std::uint8_t)
{
	active_->tx_busy_ = false;
	active_->notify_write_waiter();
	return USBD_OK;
}

//////////////////////////////////////////////////////////////////////////

void usb_cdc::reset_session()
{
	configured_ = false;
	rx_armed_ = false;
	tx_busy_ = false;
	rx_head_ = 0;
	rx_size_ = 0;
	++session_;
	notify_write_waiter();
}

//////////////////////////////////////////////////////////////////////////

void usb_cdc::notify_write_waiter()
{
	const auto waiter = write_waiter_;
	write_waiter_ = nullptr;
	if (!waiter)
	{
		return;
	}
	if (__get_IPSR() != 0)
	{
		BaseType_t higher_priority_woken = pdFALSE;
		vTaskNotifyGiveFromISR(waiter, &higher_priority_woken);
		portYIELD_FROM_ISR(higher_priority_woken);
	}
	else
	{
		xTaskNotifyGive(waiter);
	}
}

//////////////////////////////////////////////////////////////////////////

void usb_cdc::notify_receive_listener()
{
	const auto listener = receive_listener_;
	if (!listener)
	{
		return;
	}
	if (__get_IPSR() != 0)
	{
		BaseType_t higher_priority_woken = pdFALSE;
		vTaskNotifyGiveIndexedFromISR(listener, task_event_notification_index, &higher_priority_woken);
		portYIELD_FROM_ISR(higher_priority_woken);
	}
	else
	{
		xTaskNotifyGiveIndexed(listener, task_event_notification_index);
	}
}

//////////////////////////////////////////////////////////////////////////

void usb_cdc::arm_receive()
{
	const bool space_for_packet = rx_capacity - rx_size_ >= rx_packet_.size();
	if (configured_ && !rx_armed_ && space_for_packet)
	{
		rx_armed_ = USBD_CDC_ReceivePacket(&device_.native_handle()) == USBD_OK;
	}
}

//////////////////////////////////////////////////////////////////////////
