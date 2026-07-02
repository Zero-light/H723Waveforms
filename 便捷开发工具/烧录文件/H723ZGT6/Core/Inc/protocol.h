/*
 * protocol.h
 * Host communication protocol framing.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define PROTO_SOF0          0xA5
#define PROTO_SOF1          0x5A
#define PROTO_EOF           0x0A
#define PROTO_MAX_PAYLOAD   2048
#define PROTO_RX_QUEUE_DEPTH 4

typedef enum {
    CMD_WAVE_CONFIG = 0x01,
    CMD_WAVE_DATA   = 0x02,
    CMD_WAVE_CTRL   = 0x03,
    CMD_ADC_CONFIG  = 0x10,
    CMD_ADC_CTRL    = 0x11,
    CMD_ADC_DATA    = 0x12,
    CMD_SPI_CONFIG  = 0x20,
    CMD_SPI_XFER    = 0x21,
    CMD_SPI_RESP    = 0x22,
    CMD_DAC_SET     = 0x30,
    CMD_ACK         = 0xF0,
} ProtoCmd_t;

typedef struct {
    uint8_t  cmd;
    uint16_t len;
    uint8_t  payload[PROTO_MAX_PAYLOAD];
} ProtoFrame_t;

typedef void (*ProtoRxCallback_t)(const ProtoFrame_t *frame);

void PROTO_Init(ProtoRxCallback_t cb);
void PROTO_ParseByte(uint8_t byte);
bool PROTO_GetFrame(ProtoFrame_t *frame);
bool PROTO_SendFrame(const ProtoFrame_t *frame);
bool PROTO_SendAck(uint8_t cmd, bool ok);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
