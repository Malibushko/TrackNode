#include "drivers/devices/sx126x.h"

#include "common/common.h"
#include "drivers/spi_device.h"
#include "misc/bits_helpers.h"

namespace
{
	constexpr uint16_t irq_tx_done = 0x0001;
	constexpr uint16_t irq_rx_done = 0x0002;
	constexpr uint16_t irq_crc_error = 0x0040;
	constexpr uint16_t irq_timeout = 0x0200;
	constexpr uint16_t irq_all = 0x03FF;

	uint32_t radio_timeout_ticks(uint32_t timeout_ms)
	{
		const uint64_t ticks = static_cast<uint64_t>(timeout_ms) * 64;
		return static_cast<uint32_t>(ticks > 0xFFFFFE ? 0xFFFFFE : ticks);
	}

	status to_status(component component, sx126x_cmd_status_e status)
	{
		switch (status)
		{
			case sx126x_cmd_status_e::SX126X_CMD_STATUS_RESERVED:
			case sx126x_cmd_status_e::SX126X_CMD_STATUS_RFU:
			case sx126x_cmd_status_e::SX126X_CMD_STATUS_CMD_PROCESS_ERROR:
				return fail(component, error_code::invalid_argument);
			case sx126x_cmd_status_e::SX126X_CMD_STATUS_DATA_AVAILABLE:
				return {};
			case sx126x_cmd_status_e::SX126X_CMD_STATUS_CMD_TIMEOUT:
				return fail(component, error_code::timeout_error);
			case sx126x_cmd_status_e::SX126X_CMD_STATUS_CMD_EXEC_FAILURE:
				return fail(component, error_code::device_error);
			case sx126x_cmd_status_e::SX126X_CMD_STATUS_CMD_TX_DONE:
				return {};
			default:
				return fail(component, error_code::unknown_error);
		}
	}

enum sx126x_commands_e
{
    // Operational Modes Functions
    SET_SLEEP                  = 0x84,
    SET_STANDBY                = 0x80,
    SET_FS                     = 0xC1,
    SET_TX                     = 0x83,
    SET_RX                     = 0x82,
    SET_STOP_TIMER_ON_PREAMBLE = 0x9F,
    SET_RX_DUTY_CYCLE          = 0x94,
    SET_CAD                    = 0xC5,
    SET_TX_CONTINUOUS_WAVE     = 0xD1,
    SET_TX_INFINITE_PREAMBLE   = 0xD2,
    SET_REGULATOR_MODE         = 0x96,
    CALIBRATE                  = 0x89,
    CALIBRATE_IMAGE            = 0x98,
    SET_PA_CFG                 = 0x95,
    SET_RX_TX_FALLBACK_MODE    = 0x93,
    // Registers and buffer Access
    WRITE_REGISTER = 0x0D,
    READ_REGISTER  = 0x1D,
    WRITE_BUFFER   = 0x0E,
    READ_BUFFER    = 0x1E,
    // DIO and IRQ Control Functions
    SET_DIO_IRQ_PARAMS         = 0x08,
    GET_IRQ_STATUS             = 0x12,
    CLR_IRQ_STATUS             = 0x02,
    SET_DIO2_AS_RF_SWITCH_CTRL = 0x9D,
    SET_DIO3_AS_TCXO_CTRL      = 0x97,
    // RF Modulation and Packet-Related Functions
    SET_RF_FREQUENCY          = 0x86,
    SET_PKT_TYPE              = 0x8A,
    GET_PKT_TYPE              = 0x11,
    SET_TX_PARAMS             = 0x8E,
    SET_MODULATION_PARAMS     = 0x8B,
    SET_PKT_PARAMS            = 0x8C,
    SET_CAD_PARAMS            = 0x88,
    SET_BUFFER_BASE_ADDRESS   = 0x8F,
    SET_LORA_SYMB_NUM_TIMEOUT = 0xA0,
    // Communication Status Information
    GET_STATUS           = 0xC0,
    GET_RX_BUFFER_STATUS = 0x13,
    GET_PKT_STATUS       = 0x14,
    GET_RSSI_INST        = 0x15,
    GET_STATS            = 0x10,
    RESET_STATS          = 0x00,
    // Miscellaneous
    GET_DEVICE_ERRORS = 0x17,
    CLR_DEVICE_ERRORS = 0x07,
};

enum sx126x_commands_size_e
{
    // Operational Modes Functions
    SIZE_SET_SLEEP                  = 2,
    SIZE_SET_STANDBY                = 2,
    SIZE_SET_FS                     = 1,
    SIZE_SET_TX                     = 4,
    SIZE_SET_RX                     = 4,
    SIZE_SET_STOP_TIMER_ON_PREAMBLE = 2,
    SIZE_SET_RX_DUTY_CYCLE          = 7,
    SIZE_SET_CAD                    = 1,
    SIZE_SET_TX_CONTINUOUS_WAVE     = 1,
    SIZE_SET_TX_INFINITE_PREAMBLE   = 1,
    SIZE_SET_REGULATOR_MODE         = 2,
    SIZE_CALIBRATE                  = 2,
    SIZE_CALIBRATE_IMAGE            = 3,
    SIZE_SET_PA_CFG                 = 5,
    SIZE_SET_RX_TX_FALLBACK_MODE    = 2,
    // Registers and buffer Access
    // Full size: this value plus buffer size
    SIZE_WRITE_REGISTER = 3,
    // Full size: this value plus buffer size
    SIZE_READ_REGISTER = 4,
    // Full size: this value plus buffer size
    SIZE_WRITE_BUFFER = 2,
    // Full size: this value plus buffer size
    SIZE_READ_BUFFER = 3,
    // DIO and IRQ Control Functions
    SIZE_SET_DIO_IRQ_PARAMS         = 9,
    SIZE_GET_IRQ_STATUS             = 2,
    SIZE_CLR_IRQ_STATUS             = 3,
    SIZE_SET_DIO2_AS_RF_SWITCH_CTRL = 2,
    SIZE_SET_DIO3_AS_TCXO_CTRL      = 5,
    // RF Modulation and Packet-Related Functions
    SIZE_SET_RF_FREQUENCY           = 5,
    SIZE_SET_PKT_TYPE               = 2,
    SIZE_GET_PKT_TYPE               = 2,
    SIZE_SET_TX_PARAMS              = 3,
    SIZE_SET_MODULATION_PARAMS_GFSK = 9,
    SIZE_SET_MODULATION_PARAMS_LORA = 5,
    SIZE_SET_PKT_PARAMS_GFSK        = 10,
    SIZE_SET_PKT_PARAMS_LORA        = 7,
    SIZE_SET_CAD_PARAMS             = 8,
    SIZE_SET_BUFFER_BASE_ADDRESS    = 3,
    SIZE_SET_LORA_SYMB_NUM_TIMEOUT  = 2,
    // Communication Status Information
    SIZE_GET_STATUS           = 1,
    SIZE_GET_RX_BUFFER_STATUS = 2,
    SIZE_GET_PKT_STATUS       = 2,
    SIZE_GET_RSSI_INST        = 2,
    SIZE_GET_STATS            = 2,
    SIZE_RESET_STATS          = 7,
    // Miscellaneous
    SIZE_GET_DEVICE_ERRORS = 2,
    SIZE_CLR_DEVICE_ERRORS = 3,
    SIZE_MAX_BUFFER        = 255,
    SIZE_DUMMY_BYTE        = 1,
};

}
//////////////////////////////////////////////////////////////////////////

status sx126x_driver::init()
{
	if (initialized_)
	{
		return fail(component::spi_device, error_code::already_initialized);
	}

	auto gpio_status = nss_.init()
							.and_then([&]{ return reset_.init(); })
						    .and_then([&]{ return busy_.init(); })
							.and_then([&]{ return dio1_.init(); })
							.and_then([&]{ return txen_.init(); })
							.and_then([&]{ return rxen_.init(); });
	if (!gpio_status)
	{
		return gpio_status;
	}

	if (auto warmup_result = reset_chip(); !warmup_result)
	{
		return warmup_result;
	}

	const auto status = read_status(1000);
	if (!status)
	{
		return tl::unexpected(status.error());
	}

	if (*status != sx126x_status::STBY_RC)
	{
		return fail(component::spi_device, error_code::device_error);
	}

	if (auto result = configure_lora(); !result)
	{
		return result;
	}

	initialized_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

result<sx126x_status> sx126x_driver::read_status(uint32_t timeout_ms)
{
	if (auto status = wait_busy(timeout_ms); !status)
	{
		return tl::unexpected(status.error());
	}

	spi_cs_guard guard{bus_, nss_};

	uint8_t status_data[2] = {0xC0, 0x00};
	if (auto result = bus_.transmit_receive(std::span{status_data, 2}, std::span{status_data, 2}, timeout_ms); !result)
	{
		return tl::unexpected(result.error());
	}

	auto status = from_bits<sx126x_status>(status_data[1], 4, 6);

	if (status != sx126x_status::STBY_RC &&
		status != sx126x_status::STBY_XOSC &&
		status != sx126x_status::FS &&
		status != sx126x_status::RX &&
		status != sx126x_status::TX)
	{
		return fail(component::spi_device, error_code::device_error);
	}

	return status;
}

//////////////////////////////////////////////////////////////////////////

status sx126x_driver::transmit(std::span<const uint8_t> payload, uint32_t timeout_ms)
{
	if (!initialized_)
		return fail(component::spi_device, error_code::not_initialized);

	if (payload.empty() || payload.size() > 255 || timeout_ms == 0)
		return fail(component::spi_device, error_code::invalid_argument);

	auto result = set_packet_params(static_cast<uint8_t>(payload.size()), timeout_ms)
		.and_then([&] { return clear_irq_status(irq_all, timeout_ms); })
		.and_then([&] { return write_buffer(payload, timeout_ms); })
		.and_then([&] { return rxen_.write(gpio::state::reset); })
		.and_then([&] { return txen_.write(gpio::state::set); });

	if (!result)
		return result;

	const uint32_t ticks = radio_timeout_ticks(timeout_ms);
	const uint8_t tx_args[3] = {
		static_cast<uint8_t>(ticks >> 16),
		static_cast<uint8_t>(ticks >> 8),
		static_cast<uint8_t>(ticks)
	};
	result = write_command(SET_TX, std::span{tx_args}, timeout_ms)
		.and_then([&] { return wait_tx_done(timeout_ms); });

	if (!result)
	{
		const uint8_t standby_rc = 0;
		(void)write_command(SET_STANDBY, std::span{&standby_rc, 1}, timeout_ms);
	}

	const auto switch_result = txen_.write(gpio::state::reset);
	return result ? switch_result : result;
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> sx126x_driver::receive(std::span<uint8_t> buffer, uint32_t timeout_ms)
{
	if (!initialized_)
		return fail(component::spi_device, error_code::not_initialized);
	if (buffer.empty() || timeout_ms == 0)
		return fail(component::spi_device, error_code::invalid_argument);

	auto result = set_packet_params(0xFF, timeout_ms)
		.and_then([&] { return clear_irq_status(irq_all, timeout_ms); })
		.and_then([&] { return txen_.write(gpio::state::reset); })
		.and_then([&] { return rxen_.write(gpio::state::set); });
	if (!result)
		return tl::unexpected(result.error());

	const uint32_t ticks = radio_timeout_ticks(timeout_ms);
	const uint8_t rx_args[3] = {
		static_cast<uint8_t>(ticks >> 16),
		static_cast<uint8_t>(ticks >> 8),
		static_cast<uint8_t>(ticks)
	};
	result = write_command(SET_RX, std::span{rx_args}, timeout_ms)
		.and_then([&] { return wait_rx_done(timeout_ms); });
	if (!result)
	{
		const uint8_t standby_rc = 0;
		(void)write_command(SET_STANDBY, std::span{&standby_rc, 1}, timeout_ms);
	}
	const auto switch_result = rxen_.write(gpio::state::reset);
	if (!result)
		return tl::unexpected(result.error());
	if (!switch_result)
		return tl::unexpected(switch_result.error());
	return read_received_packet(buffer, timeout_ms);
}

//////////////////////////////////////////////////////////////////////////

status sx126x_driver::wait_rx_done(uint32_t timeout_ms)
{
	return wait_irq(irq_rx_done, timeout_ms);
}

//////////////////////////////////////////////////////////////////////////

status sx126x_driver::wait_tx_done(uint32_t timeout_ms)
{
	return wait_irq(irq_tx_done, timeout_ms);
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> sx126x_driver::read_received_packet(std::span<uint8_t> buffer, uint32_t timeout_ms)
{
	if (auto result = wait_busy(timeout_ms); !result)
		return tl::unexpected(result.error());

	const uint8_t status_tx[4] = { GET_RX_BUFFER_STATUS, 0, 0, 0 };
	uint8_t status_rx[4] = {};
	{
		spi_cs_guard guard{bus_, nss_};
		if (auto result = bus_.transmit_receive(status_tx, status_rx, timeout_ms); !result)
			return tl::unexpected(result.error());
	}
	const std::size_t length = status_rx[2];
	const uint8_t offset = status_rx[3];
	if (length > buffer.size())
		return fail(component::spi_device, error_code::invalid_argument);
	if (length == 0)
		return std::size_t{0};
	if (auto result = wait_busy(timeout_ms); !result)
		return tl::unexpected(result.error());

	const uint8_t read_header[3] = { READ_BUFFER, offset, 0 };
	uint8_t ignored[3] = {};
	{
		spi_cs_guard guard{bus_, nss_};
		if (auto result = bus_.transmit_receive(read_header, ignored, timeout_ms); !result)
			return tl::unexpected(result.error());

		const uint8_t dummy[32] = {};
		for (std::size_t pos = 0; pos < length;)
		{
			const std::size_t count = length - pos < sizeof(dummy) ? length - pos : sizeof(dummy);
			if (auto result = bus_.transmit_receive(std::span{dummy}.first(count), buffer.subspan(pos, count), timeout_ms); !result)
				return tl::unexpected(result.error());
			pos += count;
		}
	}
	return length;
}

//////////////////////////////////////////////////////////////////////////

status sx126x_driver::reset_chip()
{
	return reset_.write(gpio::state::set)
				.and_then([&]{ common::delay(10); return reset_.write(gpio::state::reset); });
}

//////////////////////////////////////////////////////////////////////////

status sx126x_driver::wait_busy(uint32_t timeout_ms)
{
	int32_t time_left = static_cast<int>(common::ms_to_ticks(timeout_ms));

	while (time_left >= 0)
	{
		auto start = common::get_tick();

		if (auto result = busy_.read(); !result)
		{
			return fail(component::spi_device, result.error().code);
		}
		else if (*result == gpio::state::reset)
		{
			return {};
		}

		common::delay(1);
		time_left -= static_cast<int>(common::get_tick() - start);
	}

	return fail(component::spi_device, error_code::timeout_error);
}

//////////////////////////////////////////////////////////////////////////

status sx126x_driver::write_command(uint8_t opcode, std::span<const uint8_t> arguments, uint32_t timeout_ms)
{
	if (auto result = wait_busy(timeout_ms); !result)
	{
		return result;
	}

	{
		spi_cs_guard guard{bus_, nss_};
		if (auto result = bus_.transmit(std::span{&opcode, 1}, timeout_ms); !result)
		{
			return result;
		}

		if (!arguments.empty())
		{
			if (auto result = bus_.transmit(arguments, timeout_ms); !result)
			{
				return result;
			}
		}
	}

	if (auto result = wait_busy(timeout_ms); !result)
	{
		return result;
	}

	return {};
}

status sx126x_driver::write_buffer(std::span<const uint8_t> payload, uint32_t timeout_ms)
{
	if (auto result = wait_busy(timeout_ms); !result)
		return result;
	{
		spi_cs_guard guard{bus_, nss_};
		const uint8_t header[2] = { WRITE_BUFFER, 0x00 };
		if (auto result = bus_.transmit(header, timeout_ms); !result)
			return result;
		if (auto result = bus_.transmit(payload, timeout_ms); !result)
			return result;
	}
	return wait_busy(timeout_ms);
}

result<uint16_t> sx126x_driver::read_irq_status(uint32_t timeout_ms)
{
	if (auto result = wait_busy(timeout_ms); !result)
		return tl::unexpected(result.error());
	const uint8_t tx[4] = { GET_IRQ_STATUS, 0, 0, 0 };
	uint8_t rx[4] = {};
	{
		spi_cs_guard guard{bus_, nss_};
		if (auto result = bus_.transmit_receive(tx, rx, timeout_ms); !result)
			return tl::unexpected(result.error());
	}
	return static_cast<uint16_t>((static_cast<uint16_t>(rx[2]) << 8) | rx[3]);
}

status sx126x_driver::clear_irq_status(uint16_t mask, uint32_t timeout_ms)
{
	const uint8_t args[2] = { static_cast<uint8_t>(mask >> 8), static_cast<uint8_t>(mask) };
	return write_command(CLR_IRQ_STATUS, std::span{args}, timeout_ms);
}

status sx126x_driver::wait_irq(uint16_t done_mask, uint32_t timeout_ms)
{
	const uint32_t started = common::get_tick();
	const uint32_t limit = common::ms_to_ticks(timeout_ms);
	while (common::get_tick() - started <= limit)
	{
		const auto level = dio1_.read();
		if (!level)
			return tl::unexpected(level.error());
		if (*level == gpio::state::set)
		{
			const auto irq = read_irq_status(timeout_ms);
			if (!irq)
				return tl::unexpected(irq.error());
			if (auto result = clear_irq_status(*irq, timeout_ms); !result)
				return result;
			if (*irq & irq_timeout)
				return fail(component::spi_device, error_code::timeout_error);
			if (*irq & irq_crc_error)
				return fail(component::spi_device, error_code::device_error);
			if (*irq & done_mask)
				return {};
			return fail(component::spi_device, error_code::device_error);
		}
		common::delay(1);
	}
	return fail(component::spi_device, error_code::timeout_error);
}

status sx126x_driver::set_packet_params(uint8_t payload_length, uint32_t timeout_ms)
{
	const uint8_t buf[6] = {
		static_cast<uint8_t>(config_.preamble_symbols >> 8),
		static_cast<uint8_t>(config_.preamble_symbols),
		static_cast<uint8_t>(config_.explicit_header ? 0x00 : 0x01),
		payload_length,
		static_cast<uint8_t>(config_.crc_enabled ? 0x01 : 0x00),
		static_cast<uint8_t>(config_.invert_iq ? 0x01 : 0x00)
	};
	return write_command(sx126x_commands_e::SET_PKT_PARAMS, std::span{buf}, timeout_ms);
}

status sx126x_driver::configure_lora()
{
	static constexpr uint32_t op_timeout = 500;
	if (config_.frequency_hz < 803'000'000 || config_.frequency_hz > 930'000'000)
		return fail(component::spi_device, error_code::invalid_config);
	if (!config_.explicit_header || config_.spreading_factor < 5 || config_.spreading_factor > 12 ||
		config_.tx_power_dbm < -9 || config_.tx_power_dbm > 22)
		return fail(component::spi_device, error_code::invalid_config);

	{
		uint8_t pkt_type = 0x01;
		if (auto result = write_command(sx126x_commands_e::SET_PKT_TYPE, std::span{&pkt_type, 1}, op_timeout); !result)
			return result;
	}

	{
		// Semtech encodes the lower/upper calibration frequencies in 4 MHz steps.
		const uint32_t lower_mhz = config_.frequency_hz / 1'000'000;
		const uint32_t upper_mhz = (config_.frequency_hz + 999'999) / 1'000'000;
		const auto first = static_cast<uint8_t>(lower_mhz / 4);
		auto last = static_cast<uint8_t>((upper_mhz + 3) / 4);
		if (last <= first)
			last = static_cast<uint8_t>(first + 1);
		const uint8_t buf[2] = { first, last };
		if (auto result = write_command(sx126x_commands_e::CALIBRATE_IMAGE, std::span{buf}, op_timeout); !result)
			return result;
	}

	{
		constexpr uint32_t XTAL = 32'000'000;
		auto pll = static_cast<uint32_t>((static_cast<uint64_t>(config_.frequency_hz) << 25) / XTAL);

		uint8_t buf[4] = {
			static_cast<uint8_t>((pll >> 24) & 0xFF),
			static_cast<uint8_t>((pll >> 16) & 0xFF),
			static_cast<uint8_t>((pll >>  8) & 0xFF),
			static_cast<uint8_t>( pll        & 0xFF)
		};
		if (auto result = write_command(sx126x_commands_e::SET_RF_FREQUENCY, std::span{buf}, op_timeout); !result)
			return result;
	}

	{
		uint8_t buf[4] = {
			config_.spreading_factor,
			static_cast<uint8_t>(config_.bandwidth),
			static_cast<uint8_t>(config_.coding_rate),
			static_cast<uint8_t>(config_.low_data_rate_optimization ? 0x01 : 0x00)
		};
		if (auto result = write_command(sx126x_commands_e::SET_MODULATION_PARAMS, std::span{buf}, op_timeout); !result)
			return result;
	}

	if (auto result = set_packet_params(0xFF, op_timeout); !result)
		return result;

	{
		uint8_t sw[2] = {
			static_cast<uint8_t>((config_.sync_word >> 8) & 0xFF),
			static_cast<uint8_t>( config_.sync_word       & 0xFF)
		};
		uint8_t cmd_buf[4] = { 0x07, 0x40, sw[0], sw[1] };
		if (auto result = write_command(sx126x_commands_e::WRITE_REGISTER, std::span{cmd_buf}, op_timeout); !result)
			return result;
	}

	{
		const uint8_t buf[2] = { 0x00, 0x00 };
		if (auto result = write_command(sx126x_commands_e::SET_BUFFER_BASE_ADDRESS, std::span{buf}, op_timeout); !result)
			return result;
	}

	{
		// Report completion, CRC errors and timeouts on DIO1.
		const uint8_t buf[8] = { 0x02, 0x43, 0x02, 0x43, 0x00, 0x00, 0x00, 0x00 };
		if (auto result = write_command(sx126x_commands_e::SET_DIO_IRQ_PARAMS, std::span{buf}, op_timeout); !result)
			return result;
	}
	{
		uint8_t buf[4] = {
			0x04,
			0x07,
			0x00,
			0x01
		};

		if (auto result = write_command(sx126x_commands_e::SET_PA_CFG, std::span{buf}, op_timeout); !result)
			return result;
	}

	{
		const uint8_t buf[2] = { static_cast<uint8_t>(config_.tx_power_dbm), 0x04 };
		if (auto result = write_command(sx126x_commands_e::SET_TX_PARAMS, std::span{buf}, op_timeout); !result)
			return result;
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////
