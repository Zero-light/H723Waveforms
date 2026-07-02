/*
 * bsp_usb.h
 * USB CDC hardware abstraction.
 */
#ifndef BSP_USB_H
#define BSP_USB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

bool BSP_USB_Init(void);
bool BSP_USB_Transmit(const uint8_t *data, uint16_t len);
bool BSP_USB_ReceiveByte(uint8_t *byte);
void BSP_USB_DeInit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_USB_H */
