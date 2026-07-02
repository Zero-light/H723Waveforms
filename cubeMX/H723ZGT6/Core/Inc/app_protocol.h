#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/* Frame delimiters */
#define FRAME_SOF0          0xA5
#define FRAME_SOF1          0x5A
#define FRAME_EOF           0x0A

/* Maximum payload length */
#define FRAME_MAX_PAYLOAD   2048

/* Command IDs */
typedef enum {
    CMD_WAVE_CONFIG = 0x01,
    CMD_WAVE_DATA   = 0x02,
    CMD_WAVE_CTRL   = 0x03,
    CMD_SPI_CONFIG  = 0x20,
    CMD_SPI_XFER    = 0x21,
    CMD_SPI_RESP    = 0x22,
    CMD_DAC_SET     = 0x30,
    CMD_ACK         = 0xF0,
} CmdId_t;

/* Frame structure */
typedef struct {
    uint8_t  cmd;
    uint16_t len;
    uint8_t  payload[FRAME_MAX_PAYLOAD];
} Frame_t;

/* Callback for received frames */
typedef void (*FrameRxCallback_t)(const Frame_t *frame);

/* Public API */
void APP_Protocol_Init(FrameRxCallback_t cb);
void APP_Protocol_ParseByte(uint8_t byte);
bool APP_Protocol_SendFrame(const Frame_t *frame);
bool APP_Protocol_SendAck(uint8_t cmd, bool ok);
/* Poll from main loop 鈥?returns one completed frame (ISR-safe copy) */
bool APP_Protocol_GetPendingFrame(Frame_t *out);

/* CRC helper */
uint8_t APP_Protocol_CRC8(const uint8_t *data, uint16_t len);

#endif /* APP_PROTOCOL_H */
