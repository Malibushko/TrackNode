#pragma once

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <string.h>

#define USBD_MAX_NUM_INTERFACES 2U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_SUPPORTED_CLASS 1U
#define USBD_MAX_STR_DESC_SIZ 128U
#define USBD_SELF_POWERED 1U
#define USBD_MAX_POWER 1U
#define USBD_DEBUG_LEVEL 0U
#define USBD_LPM_ENABLED 0U
#define USBD_SUPPORT_USER_STRING_DESC 0U
#define USBD_CLASS_USER_STRING_DESC 0U
#define USBD_CLASS_BOS_ENABLED 0U
#define USBD_USER_REGISTER_CALLBACK 0U
#define __IO volatile
#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#ifndef __PACKED
#define __PACKED __attribute__((packed))
#endif
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_malloc usb_class_allocate
#define USBD_free usb_class_release
#define USBD_Delay USBD_LL_Delay
#define USBD_UsrLog(...) do {} while (0)
#define USBD_ErrLog(...) do {} while (0)
#define USBD_DbgLog(...) do {} while (0)

#ifdef __cplusplus
extern "C" {
#endif
void *usb_class_allocate(uint32_t size);
void usb_class_release(void *memory);
#ifdef __cplusplus
}
#endif
