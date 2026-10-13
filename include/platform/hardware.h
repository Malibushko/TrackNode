#pragma once

namespace hardware
{
void process_pending_requests();
[[noreturn]] void request_bootloader();
}
