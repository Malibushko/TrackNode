#include "drivers/usb_device.h"

#include <array>

namespace
{
alignas(4) std::array<std::uint32_t, 256> class_memory{};
bool class_memory_used = false;

USBD_StatusTypeDef usb_status(HAL_StatusTypeDef result)
{
	return result == HAL_OK ? USBD_OK : USBD_FAIL;
}

//////////////////////////////////////////////////////////////////////////

PCD_HandleTypeDef *pcd(USBD_HandleTypeDef *device)
{
	return static_cast<PCD_HandleTypeDef *>(device->pData);
}

//////////////////////////////////////////////////////////////////////////

USBD_HandleTypeDef *stack(PCD_HandleTypeDef *handle)
{
	return static_cast<USBD_HandleTypeDef *>(handle->pData);
}

//////////////////////////////////////////////////////////////////////////
}

extern "C" void *usb_class_allocate(std::uint32_t size)
{
	if (class_memory_used || size > sizeof(class_memory))
	{
		return nullptr;
	}
	class_memory_used = true;
	return class_memory.data();
}

//////////////////////////////////////////////////////////////////////////

extern "C" void usb_class_release(void *memory)
{
	if (memory == class_memory.data())
	{
		class_memory_used = false;
	}
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *device)
{
	auto *owner = usb_device::active();
	if (!owner)
	{
		return USBD_FAIL;
	}
	auto &handle = owner->pcd_handle();
	device->pData = &handle;
	handle.pData = device;
	if (HAL_PCD_Init(&handle) != HAL_OK ||
		HAL_PCDEx_SetRxFiFo(&handle, 128) != HAL_OK ||
		HAL_PCDEx_SetTxFiFo(&handle, 0, 64) != HAL_OK ||
		HAL_PCDEx_SetTxFiFo(&handle, 1, 64) != HAL_OK ||
		HAL_PCDEx_SetTxFiFo(&handle, 2, 16) != HAL_OK)
	{
		return USBD_FAIL;
	}
	return USBD_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_MspInit(PCD_HandleTypeDef *handle)
{
	if (handle->Instance != USB_OTG_FS)
	{
		return;
	}
	__HAL_RCC_GPIOA_CLK_ENABLE();
	GPIO_InitTypeDef pins{};
	pins.Pin = GPIO_PIN_11 | GPIO_PIN_12;
	pins.Mode = GPIO_MODE_AF_PP;
	pins.Pull = GPIO_NOPULL;
	pins.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	pins.Alternate = GPIO_AF10_OTG_FS;
	HAL_GPIO_Init(GPIOA, &pins);
	__HAL_RCC_USB_OTG_FS_CLK_ENABLE();
	HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_MspDeInit(PCD_HandleTypeDef *handle)
{
	if (handle->Instance != USB_OTG_FS)
	{
		return;
	}
	HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
	__HAL_RCC_USB_OTG_FS_CLK_DISABLE();
	HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void OTG_FS_IRQHandler()
{
	if (auto *owner = usb_device::active())
	{
		HAL_PCD_IRQHandler(&owner->pcd_handle());
	}
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *handle)
{
	(void)USBD_LL_SetupStage(stack(handle), reinterpret_cast<std::uint8_t *>(handle->Setup));
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *handle, std::uint8_t endpoint)
{
	(void)USBD_LL_DataOutStage(stack(handle), endpoint, handle->OUT_ep[endpoint].xfer_buff);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *handle, std::uint8_t endpoint)
{
	(void)USBD_LL_DataInStage(stack(handle), endpoint, handle->IN_ep[endpoint].xfer_buff);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_ResetCallback(PCD_HandleTypeDef *handle)
{
	(void)USBD_LL_SetSpeed(stack(handle), USBD_SPEED_FULL);
	(void)USBD_LL_Reset(stack(handle));
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *handle)
{
	(void)USBD_LL_Suspend(stack(handle));
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *handle)
{
	(void)USBD_LL_Resume(stack(handle));
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *handle)
{
	(void)USBD_LL_DevConnected(stack(handle));
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *handle)
{
	(void)USBD_LL_DevDisconnected(stack(handle));
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_SOFCallback(PCD_HandleTypeDef *handle)
{
	(void)USBD_LL_SOF(stack(handle));
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *handle, std::uint8_t endpoint)
{
	(void)USBD_LL_IsoINIncomplete(stack(handle), endpoint);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *handle, std::uint8_t endpoint)
{
	(void)USBD_LL_IsoOUTIncomplete(stack(handle), endpoint);
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *device)
{
	return usb_status(HAL_PCD_DeInit(pcd(device)));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *device)
{
	return usb_status(HAL_PCD_Start(pcd(device)));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *device)
{
	return usb_status(HAL_PCD_Stop(pcd(device)));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *device, std::uint8_t address, std::uint8_t type, std::uint16_t size)
{
	return usb_status(HAL_PCD_EP_Open(pcd(device), address, size, type));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *device, std::uint8_t address)
{
	return usb_status(HAL_PCD_EP_Close(pcd(device), address));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *device, std::uint8_t address)
{
	return usb_status(HAL_PCD_EP_Flush(pcd(device), address));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *device, std::uint8_t address)
{
	return usb_status(HAL_PCD_EP_SetStall(pcd(device), address));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *device, std::uint8_t address)
{
	return usb_status(HAL_PCD_EP_ClrStall(pcd(device), address));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *device, std::uint8_t address)
{
	return usb_status(HAL_PCD_SetAddress(pcd(device), address));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *device, std::uint8_t address, std::uint8_t *data, std::uint32_t size)
{
	return usb_status(HAL_PCD_EP_Transmit(pcd(device), address, data, size));
}

//////////////////////////////////////////////////////////////////////////

extern "C" USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *device, std::uint8_t address, std::uint8_t *data, std::uint32_t size)
{
	return usb_status(HAL_PCD_EP_Receive(pcd(device), address, data, size));
}

//////////////////////////////////////////////////////////////////////////

extern "C" std::uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *device, std::uint8_t address)
{
	auto *handle = pcd(device);
	return (address & 0x80U) ? handle->IN_ep[address & 0x7fU].is_stall : handle->OUT_ep[address & 0x7fU].is_stall;
}

//////////////////////////////////////////////////////////////////////////

extern "C" std::uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *device, std::uint8_t address)
{
	return HAL_PCD_EP_GetRxCount(pcd(device), address);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void USBD_LL_Delay(std::uint32_t delay)
{
	HAL_Delay(delay);
}

//////////////////////////////////////////////////////////////////////////
