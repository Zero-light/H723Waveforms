/*
 * bsp_usb.c
 * H723ZG_V1 USB CDC implementation.
 */
#include "bsp_usb.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "stm32h7xx_hal.h"

bool BSP_USB_Init(void)
{
    MX_USB_DEVICE_Init();
    return true;
}

bool BSP_USB_Transmit(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) return false;
    return (CDC_Transmit_HS((uint8_t *)data, len) == USBD_OK);
}

bool BSP_USB_ReceiveByte(uint8_t *byte)
{
    (void)byte;
    /* Polling receive not implemented; protocol layer parses CDC_Receive_HS callbacks. */
    return false;
}

void BSP_USB_DeInit(void)
{
    /* Not implemented */
}
