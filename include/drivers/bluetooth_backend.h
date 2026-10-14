#pragma once

#include "core/result.h"
#include <array>
#include <span>

struct bluetooth_link
{
	bool connected = false;
	bool subscribed = false;
	std::uint16_t max_payload = 0;
	std::uint32_t session = 0;
};

struct bluetooth_event
{
	enum class kind
	{
		idle,
		link_changed,
		received,
		pairing_changed
	} type = kind::idle;

	bluetooth_link link;
	std::array<std::uint8_t, 128> payload{};
	std::size_t size = 0;
	bool pairing = false;
};

class bluetooth_backend
{
public:
	virtual ~bluetooth_backend() = default;

	virtual status init() = 0;
	virtual void stop() = 0;

	virtual status start_advertise() = 0;
	virtual status stop_advertise() = 0;

	virtual status open_pairing_window() = 0;

	virtual status send(std::span<const std::uint8_t> payload, std::uint32_t session) = 0;
	virtual status publish_measurement(std::span<const std::uint8_t> payload) = 0;
	virtual status publish_battery(std::span<const std::uint8_t>)
	{
		return fail(component::service, error_code::not_implemented);
	}

	virtual result<bluetooth_event> poll(std::uint32_t timeout_ms) = 0;
};
