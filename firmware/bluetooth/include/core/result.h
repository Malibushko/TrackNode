#pragma once

#include <tl/expected.hpp>

namespace ble
{
enum class component
{
	uart,
	protocol,
	bluetooth,
	service,
	application
};

enum class error_code
{
	not_initialized,
	not_implemented,
	invalid_config,
	not_ready,
	not_supported,
	timeout,
	io,
	already_initialized,
	busy,
	no_resources,
	not_connected,
	access_denied,
	overflow
};

struct error
{
	ble::component component;
	error_code code;
};

template <class value_type>
using result = tl::expected<value_type, error>;

using status = result<void>;

inline tl::unexpected<error> fail(component source, error_code code)
{
	return tl::make_unexpected(error{
		source,
		code
	});
}
}
