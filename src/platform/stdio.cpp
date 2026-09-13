#include "platform/stdio.h"
#include "drivers/uart.h"
#include <cerrno>

namespace
{
uart *stdout_uart = nullptr;
}

void set_stdout_uart(uart *console)
{
	stdout_uart = console;
}

extern "C" int _write(int descriptor, char *data, int length)
{
	if (descriptor != 1 && descriptor != 2)
	{
		errno = EBADF;
		return -1;
	}

	if (length < 0 || length > UINT16_MAX || (!data && length))
	{
		errno = EINVAL;
		return -1;
	}

	if (length == 0)
	{
		return 0;
	}

	if (!stdout_uart)
	{
		errno = ENODEV;
		return -1;
	}

	auto operation_status = stdout_uart->write(reinterpret_cast<const uint8_t *>(data), static_cast<size_t>(length));
	if (!operation_status)
	{
		errno = EIO;
		return -1;
	}

	return length;
}
