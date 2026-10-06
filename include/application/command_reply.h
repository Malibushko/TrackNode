#pragma once

#include "application/command.h"
#include "core/result.h"
#include <array>
#include <cstdarg>
#include <cstdio>

struct command_reply
{
	std::uint32_t request_id = 0;
	std::uint32_t session = 0;
	std::array<char, 96> text{};
};

class command_reply_sink
{
public:
	virtual ~command_reply_sink() = default;
	virtual status submit_reply(const command_reply &reply) = 0;
};

struct command_request
{
	command value{};
	command_reply_sink *reply_to = nullptr;
	std::uint32_t request_id = 0;
	std::uint32_t session = 0;
};

static_assert(std::is_trivially_copyable_v<command_request>);
static_assert(std::is_trivially_copyable_v<command_reply>);

inline status write_command_reply(const command_request &request, command_reply_sink &fallback,
                                 const char *format, std::va_list arguments)
{
	command_reply reply{request.request_id, request.session, {}};

	const auto size = std::vsnprintf(reply.text.data(), reply.text.size(), format, arguments);

	if (size < 0 || static_cast<std::size_t>(size) >= reply.text.size())
		return fail(component::service, error_code::invalid_argument);

	return (request.reply_to ? *request.reply_to : fallback).submit_reply(reply);
}
