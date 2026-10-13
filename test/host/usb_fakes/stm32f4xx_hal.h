#pragma once
#include <stdint.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t unused; } GPIO_TypeDef;
typedef struct { uint32_t PLLCFGR; uint32_t CR; } RCC_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef struct { uint8_t *xfer_buff; uint32_t xfer_len, maxpacket, is_stall; } PCD_EPTypeDef;
typedef struct
{
    uint32_t dev_endpoints, speed, dma_enable, phy_itface, Sof_enable;
    uint32_t low_power_enable, lpm_enable, vbus_sensing_enable, use_dedicated_ep1;
} PCD_InitTypeDef;
typedef struct
{
    void *Instance;
    PCD_InitTypeDef Init;
    void *pData;
    PCD_EPTypeDef IN_ep[16], OUT_ep[16];
    uint32_t Setup[12];
} PCD_HandleTypeDef;

extern GPIO_TypeDef usb_gpio;
extern RCC_TypeDef usb_rcc;
extern uint32_t usb_instance;
#define GPIOA (&usb_gpio)
#define RCC (&usb_rcc)
#define USB_OTG_FS (&usb_instance)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define GPIO_MODE_INPUT 0U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_SPEED_FREQ_VERY_HIGH 3U
#define GPIO_AF10_OTG_FS 10U
#define RCC_PLLCFGR_PLLM 0x3fU
#define RCC_PLLCFGR_PLLN (0x1ffU << 6)
#define RCC_PLLCFGR_PLLQ (0xfU << 24)
#define RCC_PLLCFGR_PLLSRC (1U << 22)
#define RCC_CR_PLLRDY (1U << 25)
#define __NVIC_PRIO_BITS 4U
#define HSE_VALUE 25000000U
#define HSI_VALUE 16000000U
#define PCD_SPEED_FULL 1U
#define PCD_PHY_EMBEDDED 2U
#define DISABLE 0U
#define OTG_FS_IRQn 67
#define UNUSED(x) ((void)(x))
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USB_OTG_FS_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USB_OTG_FS_CLK_DISABLE() ((void)0)

#ifdef __cplusplus
extern "C" {
#endif
void HAL_NVIC_SetPriority(int, uint32_t, uint32_t);
void HAL_NVIC_DisableIRQ(int);
void HAL_NVIC_EnableIRQ(int);
void HAL_NVIC_ClearPendingIRQ(int);
void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *);
void HAL_GPIO_DeInit(GPIO_TypeDef *, uint32_t);
uint32_t HAL_GetUIDw0(void);
uint32_t HAL_GetUIDw1(void);
uint32_t HAL_GetUIDw2(void);
void HAL_Delay(uint32_t);
void HAL_PCD_IRQHandler(PCD_HandleTypeDef *);
HAL_StatusTypeDef HAL_PCD_Init(PCD_HandleTypeDef *);
HAL_StatusTypeDef HAL_PCD_DeInit(PCD_HandleTypeDef *);
HAL_StatusTypeDef HAL_PCD_Start(PCD_HandleTypeDef *);
HAL_StatusTypeDef HAL_PCD_Stop(PCD_HandleTypeDef *);
HAL_StatusTypeDef HAL_PCDEx_SetRxFiFo(PCD_HandleTypeDef *, uint16_t);
HAL_StatusTypeDef HAL_PCDEx_SetTxFiFo(PCD_HandleTypeDef *, uint8_t, uint16_t);
HAL_StatusTypeDef HAL_PCD_EP_Open(PCD_HandleTypeDef *, uint8_t, uint16_t, uint8_t);
HAL_StatusTypeDef HAL_PCD_EP_Close(PCD_HandleTypeDef *, uint8_t);
HAL_StatusTypeDef HAL_PCD_EP_Flush(PCD_HandleTypeDef *, uint8_t);
HAL_StatusTypeDef HAL_PCD_EP_SetStall(PCD_HandleTypeDef *, uint8_t);
HAL_StatusTypeDef HAL_PCD_EP_ClrStall(PCD_HandleTypeDef *, uint8_t);
HAL_StatusTypeDef HAL_PCD_SetAddress(PCD_HandleTypeDef *, uint8_t);
HAL_StatusTypeDef HAL_PCD_EP_Transmit(PCD_HandleTypeDef *, uint8_t, uint8_t *, uint32_t);
HAL_StatusTypeDef HAL_PCD_EP_Receive(PCD_HandleTypeDef *, uint8_t, uint8_t *, uint32_t);
uint32_t HAL_PCD_EP_GetRxCount(PCD_HandleTypeDef *, uint8_t);
#ifdef __cplusplus
}
#endif
