#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "drivers/gpio.h"
#include "drivers/spi_bus.h"

enum class sx126x_lora_bandwidth : uint8_t
{
	KHZ_7_8 = 0x00,
	KHZ_10_4 = 0x08,
	KHZ_15_6 = 0x01,
	KHZ_20_8 = 0x09,
	KHZ_31_25 = 0x02,
	KHZ_41_7 = 0x0A,
	KHZ_62_5 = 0x03,
	KHZ_125 = 0x04,
	KHZ_250 = 0x05,
	KHZ_500 = 0x06,
};

enum class sx126x_lora_coding_rate : uint8_t
{
	CR_4_5 = 0x01,
	CR_4_6 = 0x02,
	CR_4_7 = 0x03,
	CR_4_8 = 0x04,
};

struct sx126x_config
{
	pin nss{};
	pin reset{};
	pin busy{};
	pin dio1{};
	pin txen{};
	pin rxen{};

	uint32_t frequency_hz = 0;
	uint8_t spreading_factor = 7;
	sx126x_lora_bandwidth bandwidth = sx126x_lora_bandwidth::KHZ_125;
	sx126x_lora_coding_rate coding_rate = sx126x_lora_coding_rate::CR_4_5;
	bool low_data_rate_optimization = false;
	uint16_t preamble_symbols = 8;
	bool explicit_header = true;
	bool crc_enabled = true;
	bool invert_iq = false;
	uint16_t sync_word = 0x1424;
	int8_t tx_power_dbm = 10;
};

enum class sx126x_status
{
	STBY_RC = 0x2,
	STBY_XOSC = 0x3,
	FS = 0x4,
	RX = 0x5,
	TX = 0x6
};

enum class sx126x_cmd_status_e
{
	SX126X_CMD_STATUS_RESERVED          = 0,
	SX126X_CMD_STATUS_RFU               = 1,
	SX126X_CMD_STATUS_DATA_AVAILABLE    = 2,
	SX126X_CMD_STATUS_CMD_TIMEOUT       = 3,
	SX126X_CMD_STATUS_CMD_PROCESS_ERROR = 4,
	SX126X_CMD_STATUS_CMD_EXEC_FAILURE  = 5,
	SX126X_CMD_STATUS_CMD_TX_DONE       = 6,
};

class sx126x_driver
{
public:
	explicit sx126x_driver(spi_bus &bus, sx126x_config config)
		: bus_(bus),
		  config_(config),
		  nss_({ .pin = config.nss, .mode = GPIO_MODE_OUTPUT_PP, .initial_high = true, .active_low = true }),
		  reset_({ .pin = config.reset, .mode = GPIO_MODE_OUTPUT_PP, .initial_high = true, .active_low = true }),
		  busy_({ .pin = config.busy, .mode = GPIO_MODE_INPUT }),
		  dio1_({ .pin = config.dio1, .mode = GPIO_MODE_INPUT }),
		  txen_({ .pin = config.txen, .mode = GPIO_MODE_OUTPUT_PP }),
		  rxen_({ .pin = config.rxen, .mode = GPIO_MODE_OUTPUT_PP })
	{
	}

	status init();
	result<sx126x_status> read_status(uint32_t timeout_ms);
	status transmit(std::span<const uint8_t> payload, uint32_t timeout_ms);
	result<std::size_t> receive(std::span<uint8_t> buffer, uint32_t timeout_ms);

private:
	status reset_chip();
	status wait_busy(uint32_t timeout_ms);
	status write_command(uint8_t opcode, std::span<const uint8_t> arguments, uint32_t timeout_ms);
	status write_buffer(std::span<const uint8_t> payload, uint32_t timeout_ms);
	result<uint16_t> read_irq_status(uint32_t timeout_ms);
	status clear_irq_status(uint16_t mask, uint32_t timeout_ms);
	status wait_irq(uint16_t done_mask, uint32_t timeout_ms);
	status set_packet_params(uint8_t payload_length, uint32_t timeout_ms);
	status configure_lora();
	status wait_tx_done(uint32_t timeout_ms);
	status wait_rx_done(uint32_t timeout_ms);
	result<std::size_t> read_received_packet(std::span<uint8_t> buffer, uint32_t timeout_ms);

	spi_bus &bus_;
	sx126x_config config_;
	gpio nss_;
	gpio reset_;
	gpio busy_;
	gpio dio1_;
	gpio txen_;
	gpio rxen_;

	bool initialized_{false};
};
