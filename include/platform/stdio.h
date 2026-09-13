#pragma once

class uart;

void set_stdout_uart(uart *console);
bool has_stdout_uart();
