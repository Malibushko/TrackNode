#pragma once

#include "core/result.h"

#include <cerrno>

namespace ble
{
inline error_code from_zephyr_error(int code)
{
	switch (code)
	{
	case -EALREADY:
	case -EEXIST:
		return error_code::already_initialized;
	case -EINVAL:
	case -EMSGSIZE:
		return error_code::invalid_config;
	case -ENOSYS:
	case -ENOTSUP:
		return error_code::not_supported;
	case -ENODEV:
	case -EHOSTDOWN:
		return error_code::not_ready;
	case -ETIMEDOUT:
		return error_code::timeout;
	case -EAGAIN:
	case -EBUSY:
		return error_code::busy;
	case -ENOMEM:
	case -ENOBUFS:
		return error_code::no_resources;
	case -ENOTCONN:
	case -ECONNRESET:
		return error_code::not_connected;
	case -EACCES:
	case -EPERM:
		return error_code::access_denied;
	default:
		return error_code::io;
	}
}
}
