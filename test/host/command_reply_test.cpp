#include "application/command_reply.h"
#include <cassert>
#include <cstring>

struct destination final : command_reply_sink
{
	command_reply last{};
	int calls = 0;
	bool reject = false;
	status submit_reply(const command_reply &value) override
	{ last = value; ++calls; return reject ? status{fail(component::service, error_code::queue_full)} : status{}; }
};

status send(const command_request &request, command_reply_sink &fallback, const char *format, ...)
{
	std::va_list arguments;
	va_start(arguments, format);
	auto accepted = write_command_reply(request, fallback, format, arguments);
	va_end(arguments);
	return accepted;
}

int main()
{
	destination console, ble;
	command_request request{{command_type::read_temperature}, &ble, 42, 7};
	assert(send(request, console, "temperature=%d", 20));
	assert(ble.calls == 1 && console.calls == 0);
	assert(ble.last.request_id == 42 && ble.last.session == 7);
	assert(std::strcmp(ble.last.text.data(), "temperature=20") == 0);
	assert(send({}, console, "%s", "console")); assert(console.calls == 1);
	ble.reject = true; assert(!send(request, console, "reply"));
	assert(console.calls == 1); // Do not leak BLE replies into the diagnostic console.
	char oversized[97]; std::memset(oversized, 'x', 96); oversized[96] = '\0';
	assert(!send(request, console, "%s", oversized)); assert(ble.calls == 2);
}
