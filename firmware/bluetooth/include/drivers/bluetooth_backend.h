#pragma once

#include "common/buffer.h"
#include "core/result.h"
#include "common/bluetooth_types.h"

namespace ble
{
class bluetooth_backend
{
public:
	virtual ~bluetooth_backend() = default;
	virtual status init() = 0;
	virtual void stop() = 0;

	virtual status start_advertise() = 0;
	virtual status stop_advertise() = 0;

	virtual status send(data_view data, session_t session) = 0;
	virtual void set_receive_session(session_t session) = 0;
	virtual result<bluetooth_event> poll(std::uint32_t timeout_ms) = 0;
};
}
