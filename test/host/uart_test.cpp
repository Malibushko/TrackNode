#include "drivers/uart.h"
#include "drivers/uart_serial_device.h"
#include <stdexcept>

namespace
{
uart *active_uart = nullptr;
unsigned rx_pull = GPIO_NOPULL;
unsigned tx_pull = GPIO_NOPULL;

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("UART error isolation regression");
	}
}

void receive_error()
{
	auto &handle = active_uart->handle();
	handle.ErrorCode = HAL_UART_ERROR_FE | HAL_UART_ERROR_NE;
	handle.RxState = HAL_UART_STATE_READY;
	HAL_UART_ErrorCallback(&handle);
}

void receive_error_then_complete_tx()
{
	receive_error();
	HAL_UART_TxCpltCallback(&active_uart->handle());
}

void dma_error()
{
	auto &handle = active_uart->handle();
	handle.ErrorCode = HAL_UART_ERROR_DMA;
	handle.RxState = HAL_UART_STATE_READY;
	HAL_UART_ErrorCallback(&handle);
}
}

status gpio::init()
{
	if (config_.pin.mask == 1)
	{
		tx_pull = config_.pull;
	}
	else
	{
		rx_pull = config_.pull;
	}
	return {};
}

int main()
{
	uart serial{uart_config{
		.instance = USART1,
		.tx = {GPIOA, 1},
		.rx = {GPIOA, 2},
		.alternate = 7,
		.baud_rate = 115200,
		.mode = UART_MODE_TX_RX,
		.rx_pull = GPIO_PULLUP
	}};
	active_uart = &serial;
	require(serial.init().has_value());
	require(rx_pull == GPIO_PULLUP && tx_pull == GPIO_NOPULL);
	std::uint8_t byte = 42;

	fake_wait_hook = receive_error_then_complete_tx;
	require(serial.write(&byte, 1).has_value());
	require(fake_notifications == 1 && fake_tx_aborts == 0);
	require(serial.handle().ErrorCode == HAL_UART_ERROR_NONE);
	auto received = serial.read(&byte, 1, 0);
	require(!received && received.error().hal_detail == (HAL_UART_ERROR_FE | HAL_UART_ERROR_NE));
	require(serial.read(&byte, 1, 0).has_value());

	const auto before_nonblocking_error = fake_rx_starts;
	serial.handle().ErrorCode = HAL_UART_ERROR_PE;
	serial.handle().RxState = HAL_UART_STATE_BUSY_RX;
	HAL_UART_ErrorCallback(&serial.handle());
	require(fake_rx_starts == before_nonblocking_error);
	received = serial.read(&byte, 1, 0);
	require(!received && received.error().hal_detail == HAL_UART_ERROR_PE);

	fake_wait_hook = dma_error;
	auto transmitted = serial.write(&byte, 1);
	require(!transmitted && transmitted.error().code == error_code::device_error);
	require(transmitted.error().hal_detail == HAL_UART_ERROR_DMA);
	require(fake_notifications == 2 && fake_tx_aborts == 1);
	received = serial.read(&byte, 1, 0);
	require(!received && received.error().hal_detail == HAL_UART_ERROR_DMA);
	fake_wait_hook = nullptr;
	transmitted = serial.write(&byte, 1);
	require(!transmitted && transmitted.error().code == error_code::timeout_error);
	require(transmitted.error().hal_detail == 0);

	fake_receive_hook = receive_error;
	received = serial.read(&byte, 1, 0);
	require(!received && received.error().hal_detail == (HAL_UART_ERROR_FE | HAL_UART_ERROR_NE));
	fake_receive_hook = nullptr;

	fake_rx_start_result = HAL_ERROR;
	receive_error();
	received = serial.read(&byte, 1, 0);
	require(!received && received.error().code == error_code::hal_failure);
	require(received.error().hal_detail == (HAL_UART_ERROR_FE | HAL_UART_ERROR_NE));
	fake_rx_start_result = HAL_OK;
	received = serial.read(&byte, 1, 0);
	require(!received && received.error().hal_detail == (HAL_UART_ERROR_FE | HAL_UART_ERROR_NE));
	require(serial.handle().RxState == HAL_UART_STATE_BUSY_RX);
	require(serial.read(&byte, 1, 0).has_value());

	uart_serial_device adapter{serial};
	serial_device &transport = adapter;
	fake_wait_hook = receive_error_then_complete_tx;
	const auto written = transport.write(&byte, 1);
	require(written && *written == 1);
	auto unsupported_read = transport.read_async(&byte, 1);
	require(unsupported_read.await_ready());
	const auto unsupported = unsupported_read.await_resume();
	require(!unsupported && unsupported.error().code == error_code::not_implemented);
	{
		auto operation = transport.write_async(&byte, 1);
		require(!operation.await_ready());
		require(!transport.write(&byte, 1));
		HAL_UART_TxCpltCallback(&serial.handle());
		const auto completed = operation.await_resume();
		require(completed && *completed == 1);
	}
	const auto aborts_before_cancel = fake_tx_aborts;
	{
		auto abandoned = transport.write_async(&byte, 1);
		require(!abandoned.await_ready());
	}
	require(fake_tx_aborts == aborts_before_cancel + 1);
	fake_wait_hook = nullptr;
	{
		auto timeout = transport.write_async(&byte, 1);
		require(!timeout.await_ready());
		const auto failed = timeout.await_resume();
		require(!failed && failed.error().code == error_code::timeout_error);
	}
	{
		auto empty = transport.write_async(nullptr, 0);
		require(empty.await_ready());
	}
	{
		auto empty = transport.write_async(nullptr, 0);
		require(empty.await_ready());
		require(empty.await_resume() == 0);
	}
}
