#include "drivers/devices/zephyr_uart.h"
#include <zephyr/drivers/uart.h>
#include <array>
#include <iostream>
#include <stdexcept>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("UART test failed");
	}
}

int main()
{
	device hardware;
	ble::zephyr_uart transport{hardware, {.baud_rate = 115200}};
	std::array<std::uint8_t, 256> output{};
	const ble::mutable_data_view destination{output.data(), output.size()};
	const std::array<std::uint8_t, 20> payload{1, 0, 2, 3, 4, 5, 6, 7, 8, 9};
	const ble::data_view source{payload.data(), payload.size()};

	require(transport.read(destination, 0).error().code == ble::error_code::not_initialized);
	require(transport.write(source, 10).error().code == ble::error_code::not_initialized);
	hardware.ready = false;
	require(transport.init().error().code == ble::error_code::not_ready);
	hardware.ready = true;
	hardware.configure_result = -ENOTSUP;
	require(transport.init().error().code == ble::error_code::not_supported);
	hardware.configure_result = -EINVAL;
	require(transport.init().error().code == ble::error_code::invalid_config);
	hardware.configure_result = 0;
	hardware.callback_result = -ENOSYS;
	require(transport.init().error().code == ble::error_code::not_supported);
	hardware.callback_result = 0;
	require(transport.init().has_value());
	require(transport.init().has_value());

	pump = [&] { fire(hardware); };
	require(transport.write(source, 10).has_value());
	require(hardware.outgoing == std::vector<std::uint8_t>(payload.begin(), payload.end()));
	require(!hardware.tx);
	const auto previous_size = hardware.outgoing.size();
	require(transport.write(source, 0).error().code == ble::error_code::timeout);
	require(hardware.outgoing.size() == previous_size);
	hardware.stalled = true;
	require(transport.write(source, 3).error().code == ble::error_code::timeout);
	require(!hardware.tx);
	hardware.stalled = false;
	hardware.fill_failure = true;
	require(transport.write(source, 10).error().code == ble::error_code::io);
	hardware.fill_failure = false;
	require(transport.write(source, 10).has_value());

	hardware.incoming.insert(hardware.incoming.end(), payload.begin(), payload.end());
	fire(hardware);
	const auto first_read = transport.read({output.data(), 3}, 0);
	require(first_read && *first_read == 3);
	require(std::equal(output.begin(), output.begin() + 3, payload.begin()));
	const auto rest = transport.read(destination, 0);
	require(rest && *rest == payload.size() - 3);
	require(std::equal(output.begin(), output.begin() + *rest, payload.begin() + 3));
	require(transport.read(destination, 2).error().code == ble::error_code::timeout);
	pump = [&]
	{
		hardware.incoming.push_back(42);
		fire(hardware);
	};
	const auto delayed = transport.read(destination, 10);
	require(delayed && *delayed == 1 && output[0] == 42);

	hardware.incoming.insert(hardware.incoming.end(), 1100, 9);
	fire(hardware);
	require(transport.read(destination, 0).error().code == ble::error_code::io);
	require(transport.read(destination, 0).error().code == ble::error_code::timeout);
	hardware.errors = 1;
	fire(hardware);
	require(transport.read(destination, 0).error().code == ble::error_code::io);
	hardware.incoming.push_back(23);
	fire(hardware);
	const auto recovered = transport.read(destination, 0);
	require(recovered && *recovered == 1 && output[0] == 23);
	require(transport.read({nullptr, 0}, 0).value() == 0);
	require(transport.write({nullptr, 0}, 0).has_value());
	require(transport.read({nullptr, 1}, 0).error().code == ble::error_code::invalid_config);
	require(transport.write({nullptr, 1}, 1).error().code == ble::error_code::invalid_config);

	pump = [&] { transport.stop(); };
	require(transport.write(source, 10).error().code == ble::error_code::not_initialized);
	require(!hardware.rx && !hardware.tx && !hardware.errors_enabled);
	transport.stop();
	require(transport.init().has_value());
	pump = [&] { transport.stop(); };
	require(transport.read(destination, 10).error().code == ble::error_code::not_initialized);
	require(transport.init().has_value());
	pump = [&] { fire(hardware); };
	require(transport.write(source, 10).has_value());
	transport.stop();

	std::cout << "UART lifecycle, buffering, errors, timeout and TX completion tests passed\n";
	return 0;
}
