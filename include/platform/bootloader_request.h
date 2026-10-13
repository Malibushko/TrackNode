#pragma once

#include <cstdint>

struct bootloader_request
{
	std::uint32_t magic;
	std::uint32_t inverse;
};

inline constexpr std::uint32_t bootloader_request_magic = 0x424F4F54;

inline bool consume_bootloader_request(volatile bootloader_request &request, bool software_reset)
{
	const bool valid_request = request.magic == bootloader_request_magic &&
		request.inverse == ~bootloader_request_magic;
	request.magic = 0;
	request.inverse = 0;
	return software_reset && valid_request;
}
