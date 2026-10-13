#include "drivers/usb_cdc.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

GPIO_TypeDef usb_gpio{};
RCC_TypeDef usb_rcc{25U | (288U << 6) | (6U << 24) | RCC_PLLCFGR_PLLSRC, RCC_CR_PLLRDY};
uint32_t usb_instance = 0;
bool irq_enabled = false;
bool cable_attached = false;
bool fail_init = false;
bool fail_start = false;
int rx_arms = 0;
int tx_submissions = 0;
uint32_t rx_count = 0;

extern "C" void HAL_NVIC_SetPriority(int, uint32_t, uint32_t) {}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_NVIC_DisableIRQ(int)
{
    irq_enabled = false;
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_NVIC_EnableIRQ(int)
{
    irq_enabled = true;
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_NVIC_ClearPendingIRQ(int) {}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *) {}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_GPIO_DeInit(GPIO_TypeDef *, uint32_t) {}

//////////////////////////////////////////////////////////////////////////

extern "C" uint32_t HAL_GetUIDw0()
{
    return 1;
}

//////////////////////////////////////////////////////////////////////////

extern "C" uint32_t HAL_GetUIDw1()
{
    return 2;
}

//////////////////////////////////////////////////////////////////////////

extern "C" uint32_t HAL_GetUIDw2()
{
    return 3;
}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_Delay(uint32_t) {}

//////////////////////////////////////////////////////////////////////////

extern "C" void HAL_PCD_IRQHandler(PCD_HandleTypeDef *) {}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_Init(PCD_HandleTypeDef *)
{
    return fail_init ? HAL_ERROR : HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_DeInit(PCD_HandleTypeDef *)
{
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_Start(PCD_HandleTypeDef *)
{
    cable_attached = !fail_start;
    return fail_start ? HAL_ERROR : HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_Stop(PCD_HandleTypeDef *)
{
    cable_attached = false;
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCDEx_SetRxFiFo(PCD_HandleTypeDef *, uint16_t)
{
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCDEx_SetTxFiFo(PCD_HandleTypeDef *, uint8_t, uint16_t)
{
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_EP_Open(PCD_HandleTypeDef *pcd, uint8_t ep, uint16_t size, uint8_t)
{
    (ep & 0x80 ? pcd->IN_ep[ep & 15] : pcd->OUT_ep[ep & 15]).maxpacket = size;
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_EP_Close(PCD_HandleTypeDef *, uint8_t)
{
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_EP_Flush(PCD_HandleTypeDef *, uint8_t)
{
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_EP_SetStall(PCD_HandleTypeDef *, uint8_t)
{
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_EP_ClrStall(PCD_HandleTypeDef *, uint8_t)
{
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_SetAddress(PCD_HandleTypeDef *, uint8_t)
{
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_EP_Transmit(PCD_HandleTypeDef *pcd, uint8_t ep, uint8_t *data, uint32_t size)
{
    pcd->IN_ep[ep & 15].xfer_buff = data;
    pcd->IN_ep[ep & 15].xfer_len = size;
    ++tx_submissions;
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" HAL_StatusTypeDef HAL_PCD_EP_Receive(PCD_HandleTypeDef *pcd, uint8_t ep, uint8_t *data, uint32_t size)
{
    pcd->OUT_ep[ep & 15].xfer_buff = data;
    pcd->OUT_ep[ep & 15].xfer_len = size;
    ++rx_arms;
    return HAL_OK;
}

//////////////////////////////////////////////////////////////////////////

extern "C" uint32_t HAL_PCD_EP_GetRxCount(PCD_HandleTypeDef *, uint8_t)
{
    return rx_count;
}

//////////////////////////////////////////////////////////////////////////


void enumerate(usb_device &device)
{
    auto &stack = device.native_handle();
    assert(USBD_LL_SetSpeed(&stack, USBD_SPEED_FULL) == USBD_OK);
    assert(USBD_LL_Reset(&stack) == USBD_OK);
    stack.dev_state = USBD_STATE_ADDRESSED;
    uint8_t setup[]{0, USB_REQ_SET_CONFIGURATION, 1, 0, 0, 0, 0, 0};
    assert(USBD_LL_SetupStage(&stack, setup) == USBD_OK);
    assert(device.configured());
}

//////////////////////////////////////////////////////////////////////////

void receive(usb_device &device, uint8_t fill, uint32_t size)
{
    auto &pcd = device.pcd_handle();
    assert(size <= pcd.OUT_ep[1].xfer_len);
    std::memset(pcd.OUT_ep[1].xfer_buff, fill, size);
    rx_count = size;
    assert(USBD_LL_DataOutStage(&device.native_handle(), 1, pcd.OUT_ep[1].xfer_buff) == USBD_OK);
}

//////////////////////////////////////////////////////////////////////////

int main()
{
    usb_device invalid{{}};
    assert(!invalid.init());
    usb_device device{{.vendor_id = 0x1234, .product_id = 0x5678}};
    usb_cdc serial{device};
    assert(!serial.init());
    const auto valid_pll = usb_rcc.PLLCFGR;
    usb_rcc.PLLCFGR = 0;
    assert(!device.init());
    usb_rcc.PLLCFGR = valid_pll;
    fail_init = true;
    assert(!device.init());
    assert(usb_device::active() == nullptr);
    fail_init = false;
    assert(device.init());
    usb_device second{{.vendor_id = 0x1234, .product_id = 0x5679}};
    assert(!second.init());
    assert(!device.start());
    assert(serial.init());
    assert(!serial.init());
    assert(!device.register_function(USBD_CDC, reinterpret_cast<void *>(1), {}));
    assert(serial.start());
    assert(!cable_attached && !irq_enabled && !serial.connected());
    fail_start = true;
    assert(!device.set_vbus_present(true));
    assert(!cable_attached && !irq_enabled);
    fail_start = false;
    assert(device.set_vbus_present(true));
    assert(cable_attached && irq_enabled);
    enumerate(device);
    assert(serial.connected());
    second.stop();
    assert(irq_enabled && serial.connected());
    uint16_t length = 0;
    auto *descriptor = device.native_handle().pDesc->GetDeviceDescriptor(USBD_SPEED_FULL, &length);
    assert(length == 18 && descriptor[4] == 2 && descriptor[5] == 0 && descriptor[6] == 0);
    assert(descriptor[8] == 0x34 && descriptor[9] == 0x12);
    auto *configuration = static_cast<uint8_t *>(device.native_handle().pConfDesc);
    assert(configuration[4] == 2 && (configuration[7] & 0x40) && configuration[8] == 0);
    bool acm_interface_found = false;
    for (uint16_t offset = 0; offset < 67; offset += configuration[offset])
    {
        assert(configuration[offset] != 0);
        if (configuration[offset + 1] == USB_DESC_TYPE_INTERFACE && configuration[offset + 5] == 2)
        {
            assert(configuration[offset + 6] == 2);
            acm_interface_found = true;
        }
    }
    assert(acm_interface_found);
    uint8_t get_coding[]{0xa1, CDC_GET_LINE_CODING, 0, 0, 0, 0, 7, 0};
    assert(USBD_LL_SetupStage(&device.native_handle(), get_coding) == USBD_OK);
    assert(device.pcd_handle().IN_ep[0].xfer_len == 7);
    assert(device.pcd_handle().IN_ep[0].xfer_buff[1] == 0xc2);

    std::array<uint8_t, 1100> buffer{};
    assert(serial.read(buffer.data(), buffer.size()) == 0);
    assert(!serial.read(nullptr, 1));
    receive(device, 0x31, 64);
    assert(serial.available() == 64);
    assert(serial.read(buffer.data(), 13) == 13);
    assert(std::all_of(buffer.begin(), buffer.begin() + 13, [](auto v) { return v == 0x31; }));
    assert(serial.read(buffer.data(), buffer.size()) == 51);
    for (int i = 0; i < 16; ++i)
    {
        receive(device, static_cast<uint8_t>(i), 64);
    }
    assert(serial.available() == 1024);
    const int arms_before = rx_arms;
    assert(serial.read(buffer.data(), 1) == 1);
    assert(rx_arms == arms_before);
    assert(serial.read(buffer.data() + 1, 63) == 63);
    assert(rx_arms == arms_before + 1);
    receive(device, 0xa5, 64);
    assert(serial.read(buffer.data(), buffer.size()) == 1024);
    for (int i = 0; i < 15; ++i)
    {
        assert(std::all_of(buffer.begin() + i * 64, buffer.begin() + (i + 1) * 64,
            [i](auto v) { return v == i + 1; }));
    }
    assert(std::all_of(buffer.begin() + 960, buffer.begin() + 1024, [](auto v) { return v == 0xa5; }));

    std::fill(buffer.begin(), buffer.end(), 0x42);
    assert(!serial.write(nullptr, 1));
    assert(serial.write(buffer.data(), 300) == 256);
    assert(serial.tx_busy());
    buffer.fill(0);
    auto &pcd = device.pcd_handle();
    assert(pcd.IN_ep[1].xfer_buff[0] == 0x42);
    assert(!serial.write(buffer.data(), 1));
    assert(USBD_LL_DataInStage(&device.native_handle(), 1, pcd.IN_ep[1].xfer_buff) == USBD_OK);
    assert(serial.tx_busy() && pcd.IN_ep[1].xfer_len == 0);
    assert(USBD_LL_DataInStage(&device.native_handle(), 1, nullptr) == USBD_OK);
    assert(!serial.tx_busy());
    receive(device, 0x55, 4);
    const auto before_reset = serial.session();
    enumerate(device);
    assert(serial.available() == 0 && serial.session() != before_reset);
    assert(serial.write(buffer.data(), 3) == 3);
    receive(device, 0x22, 7);
    const auto old_session = serial.session();
    assert(device.set_vbus_present(false));
    assert(!serial.connected() && serial.available() == 0 && !serial.tx_busy());
    assert(serial.session() != old_session);
    assert(!serial.read(buffer.data(), 1) && !serial.write(buffer.data(), 1));
    assert(device.set_vbus_present(true));
    enumerate(device);
    assert(serial.available() == 0);
    assert(USBD_LL_Suspend(&device.native_handle()) == USBD_OK);
    assert(!serial.connected() && !serial.write(buffer.data(), 1));
    assert(USBD_LL_Resume(&device.native_handle()) == USBD_OK);
    assert(serial.connected());
    receive(device, 0x66, 11);
    assert(USBD_LL_DevDisconnected(&device.native_handle()) == USBD_OK);
    assert(!serial.connected() && serial.available() == 0);
    enumerate(device);

    serial.stop();
    assert(serial.start());
    enumerate(device);
    serial.deinit();
    assert(usb_device::active() == nullptr);
    assert(!serial.start());
    assert(device.init());
    assert(serial.init());
    assert(device.set_vbus_present(true));
    assert(serial.start());
    enumerate(device);
    serial.deinit();
    assert(usb_critical_depth == 0);
    std::cout << "USB device/CDC with real ST core: lifecycle, descriptors, RX backpressure, TX copy/ZLP, reset/disconnect PASS\n";
}
