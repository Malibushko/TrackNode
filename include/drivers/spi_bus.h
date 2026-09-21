#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "platform/hal.h"
#include "core/hardware_object.h"
#include "drivers/gpio.h"

struct spi_bus_config
{
    SPI_TypeDef *instance = nullptr;
    uint32_t mode = SPI_MODE_MASTER;
    uint32_t direction = SPI_DIRECTION_2LINES;
    uint32_t data_size = SPI_DATASIZE_8BIT;
    uint32_t clk_polarity = SPI_POLARITY_LOW;
    uint32_t clk_phase = SPI_PHASE_1EDGE;
    uint32_t nss = SPI_NSS_SOFT;
    uint32_t baud_rate_prescaler = SPI_BAUDRATEPRESCALER_16;
    uint32_t first_bit = SPI_FIRSTBIT_MSB;
    uint32_t ti_mode = SPI_TIMODE_DISABLE;
    uint32_t crc_calculation = SPI_CRCCALCULATION_DISABLE;
    uint32_t crc_polynomial = 10;

    pin pin_sck{};
    pin pin_miso{};
    pin pin_mosi{};
};

class spi_bus : hardware_object
{
	static const uint32_t DEFAULT_TIMEOUT = 100;
public:
    using handle_t = SPI_HandleTypeDef;

    static constexpr uint8_t default_read_flag = 0x80;

public:
    explicit spi_bus(spi_bus_config config) : spi_handle(), config_(config)
    {
    }

    status init();

    status transmit(std::span<const uint8_t> data, uint32_t timeout = DEFAULT_TIMEOUT);
    status receive(std::span<uint8_t> data, uint32_t timeout = DEFAULT_TIMEOUT);
    status transmit_receive(std::span<const uint8_t> tx, std::span<uint8_t> rx, uint32_t timeout = DEFAULT_TIMEOUT);

    handle_t &handle()
    {
        return spi_handle;
    }

	status set_baud_rate_prescaler(uint32_t prescaler);

private:
    status validate_transfer(std::size_t size) const;

    handle_t spi_handle;
    spi_bus_config config_;
    bool attempted_{ false };
    bool initialized_ = false;
};