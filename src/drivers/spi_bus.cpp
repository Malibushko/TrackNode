#include "drivers/spi_bus.h"

#include <optional>

namespace
{

bool enable_spi_clock(const SPI_TypeDef *instance)
{
    if (instance == SPI1)
    {
        __HAL_RCC_SPI1_CLK_ENABLE();
        return true;
    }
#ifdef SPI2
    if (instance == SPI2)
    {
        __HAL_RCC_SPI2_CLK_ENABLE();
        return true;
    }
#endif
#ifdef SPI3
    if (instance == SPI3)
    {
        __HAL_RCC_SPI3_CLK_ENABLE();
        return true;
    }
#endif
    return false;
}

//////////////////////////////////////////////////////////////////////////

std::optional<uint32_t> spi_alternate_function(const SPI_TypeDef *instance)
{
    if (instance == SPI1)
    {
        return GPIO_AF5_SPI1;
    }
#ifdef SPI2
    if (instance == SPI2)
    {
        return GPIO_AF5_SPI2;
    }
#endif
#ifdef SPI3
    if (instance == SPI3)
    {
        return GPIO_AF6_SPI3;
    }
#endif
    return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////

}  // namespace

status spi_bus::init()
{
    if (initialized_) {
        return fail(component::spi_bus, error_code::already_initialized);
    }

    attempted_ = true;

    if (config_.instance == nullptr || !enable_spi_clock(config_.instance)) {
        return fail(component::spi_bus, error_code::invalid_config);
    }

    const std::optional<uint32_t> alternate = spi_alternate_function(config_.instance);
    if (!alternate) {
        return fail(component::spi_bus, error_code::invalid_config);
    }

    auto init_pin = [&](const pin &p, uint32_t pull) -> status
    {
       if (p.port == nullptr) {
          return fail(component::spi_bus, error_code::invalid_config);
       }

       gpio_config pin_config{};
       pin_config.pin = p;
       pin_config.mode = GPIO_MODE_AF_PP;
       pin_config.pull = pull;
       pin_config.speed = GPIO_SPEED_FREQ_VERY_HIGH;
       pin_config.alternate = *alternate;

       gpio configured_pin{ pin_config };

       return configured_pin.init();
    };

    const uint32_t sck_pull = (config_.clk_polarity == SPI_POLARITY_LOW) ? GPIO_PULLDOWN : GPIO_PULLUP;
    auto result = init_pin(config_.pin_sck, sck_pull)
                 .and_then([&]() -> status { return init_pin(config_.pin_miso, GPIO_NOPULL); })
                 .and_then([&]() -> status { return init_pin(config_.pin_mosi, GPIO_NOPULL); });

    if (!result)
    {
       return result;
    }

    spi_handle.Instance = config_.instance;
    spi_handle.Init.Mode = config_.mode;
    spi_handle.Init.Direction = config_.direction;
    spi_handle.Init.DataSize = config_.data_size;
    spi_handle.Init.CLKPolarity = config_.clk_polarity;
    spi_handle.Init.CLKPhase = config_.clk_phase;
    spi_handle.Init.NSS = config_.nss;
    spi_handle.Init.BaudRatePrescaler = config_.baud_rate_prescaler;
    spi_handle.Init.FirstBit = config_.first_bit;
    spi_handle.Init.TIMode = config_.ti_mode;
    spi_handle.Init.CRCCalculation = config_.crc_calculation;
    spi_handle.Init.CRCPolynomial = config_.crc_polynomial;

    result = from_hal(component::spi_bus, HAL_SPI_Init(&spi_handle));
    initialized_ = result.has_value();
    return result;
}

//////////////////////////////////////////////////////////////////////////

status spi_bus::transmit(std::span<const uint8_t> data, uint32_t timeout)
{
    if (const status s = validate_transfer(data.size()); !s) {
        return s;
    }

    return from_hal(component::spi_bus, HAL_SPI_Transmit(&spi_handle, const_cast<uint8_t *>(data.data()), static_cast<uint16_t>(data.size()), timeout));
}

//////////////////////////////////////////////////////////////////////////

status spi_bus::receive(std::span<uint8_t> data, uint32_t timeout)
{
    if (const status s = validate_transfer(data.size()); !s) {
        return s;
    }
    return from_hal(component::spi_bus, HAL_SPI_Receive(&spi_handle, data.data(), static_cast<uint16_t>(data.size()), timeout));
}

//////////////////////////////////////////////////////////////////////////

status spi_bus::transmit_receive(std::span<const uint8_t> tx, std::span<uint8_t> rx, uint32_t timeout)
{
    if (tx.size() != rx.size()) {
        return fail(component::spi_bus, error_code::invalid_argument);
    }

    if (const status s = validate_transfer(tx.size()); !s) {
        return s;
    }

    return from_hal(component::spi_bus, HAL_SPI_TransmitReceive(&spi_handle, const_cast<uint8_t *>(tx.data()), rx.data(), static_cast<uint16_t>(tx.size()), timeout));
}

//////////////////////////////////////////////////////////////////////////

status spi_bus::set_baud_rate_prescaler(uint32_t prescaler)
{
	if (!IS_SPI_BAUDRATE_PRESCALER(prescaler))
	{
		return fail(component::spi_bus, error_code::invalid_argument);
	}

	__HAL_SPI_DISABLE(&spi_handle);
	MODIFY_REG(spi_handle.Instance->CR1, SPI_CR1_BR, prescaler);
	spi_handle.Init.BaudRatePrescaler = prescaler;
	__HAL_SPI_ENABLE(&spi_handle);

	return {};
}

//////////////////////////////////////////////////////////////////////////

status spi_bus::validate_transfer(std::size_t size) const
{
    if (!initialized_) {
        return fail(component::spi_bus, error_code::not_initialized);
    }

    if (size == 0 || size > UINT16_MAX) {
        return fail(component::spi_bus, error_code::invalid_argument);
    }

    return {};
}

//////////////////////////////////////////////////////////////////////////
