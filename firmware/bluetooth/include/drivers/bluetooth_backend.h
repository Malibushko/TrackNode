#pragma once

#include "common/buffer.h"
#include "core/result.h"
#include "common/bluetooth_types.h"
#include <optional>
#include "drivers/protocols/bluetooth_measurement.h"
#include "drivers/protocols/bluetooth_battery.h"

namespace ble
{
class bluetooth_backend
{
public:
	virtual ~bluetooth_backend() = default;

	virtual status init() = 0;
	virtual void stop() = 0;

	virtual status open_pairing_window() = 0;
	virtual bool pairing_active() const = 0;

	virtual status start_advertise() = 0;
	virtual status stop_advertise() = 0;

	virtual status send(data_view data, session_t session) = 0;
	virtual void set_receive_session(session_t session) = 0;

	virtual status update_measurement(const bluetooth_measurement::snapshot &value) = 0;
	virtual void reset_measurement() = 0;
	virtual status update_battery(const bluetooth_battery::snapshot &)
	{
		return fail(component::bluetooth, error_code::not_implemented);
	}

	virtual result<bluetooth_event> poll(std::uint32_t timeout_ms) = 0;
	[[nodiscard]] virtual std::optional<std::uint32_t> next_deadline_ms() const
	{
		return std::nullopt;
	}
};
}
