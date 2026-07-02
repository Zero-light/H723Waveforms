/*
 * protocol.c
 * Host communication protocol implementation.
 */
#include "protocol.h"
#include "crc8.h"
#include "bsp_usb.h"
#include <string.h>

static uint8_t s_txBuf[PROTO_MAX_PAYLOAD + 8];
static ProtoRxCallback_t s_rxCb = NULL;

typedef enum {
    ST_IDLE,
    ST_SOF0,
    ST_CMD,
    ST_LEN_LO,
    ST_LEN_HI,
    ST_PAYLOAD,
    ST_CRC,
    ST_EOF,
} ParseState_t;

static struct {
    ParseState_t state;
    uint8_t  cmd;
    uint16_t len;
    uint16_t idx;
    uint8_t  payload[PROTO_MAX_PAYLOAD];
    uint8_t  crc;
} s_parser;

static uint8_t calc_frame_crc(uint8_t cmd, uint16_t len, const uint8_t *payload)
{
    uint8_t crc = 0x00;
    crc = CRC8_Update(crc, cmd);
    crc = CRC8_Update(crc, (uint8_t)(len & 0xFF));
    crc = CRC8_Update(crc, (uint8_t)((len >> 8) & 0xFF));
    for (uint16_t i = 0; i < len; i++) {
        crc = CRC8_Update(crc, payload[i]);
    }
    return crc;
}

void PROTO_Init(ProtoRxCallback_t cb)
{
    s_rxCb = cb;
    memset(&s_parser, 0, sizeof(s_parser));
    s_parser.state = ST_IDLE;
}

void PROTO_ParseByte(uint8_t byte)
{
    switch (s_parser.state) {
        case ST_IDLE:
            if (byte == PROTO_SOF0) s_parser.state = ST_SOF0;
            break;

        case ST_SOF0:
            s_parser.state = (byte == PROTO_SOF1) ? ST_CMD : ST_IDLE;
            break;

        case ST_CMD:
            s_parser.cmd = byte;
            s_parser.state = ST_LEN_LO;
            break;

        case ST_LEN_LO:
            s_parser.len = byte;
            s_parser.state = ST_LEN_HI;
            break;

        case ST_LEN_HI:
            s_parser.len |= ((uint16_t)byte << 8);
            if (s_parser.len > PROTO_MAX_PAYLOAD) {
                s_parser.state = ST_IDLE;
            } else if (s_parser.len == 0) {
                s_parser.state = ST_CRC;
            } else {
                s_parser.idx = 0;
                s_parser.state = ST_PAYLOAD;
            }
            break;

        case ST_PAYLOAD:
            s_parser.payload[s_parser.idx++] = byte;
            if (s_parser.idx >= s_parser.len) {
                s_parser.state = ST_CRC;
            }
            break;

        case ST_CRC:
            s_parser.crc = byte;
            s_parser.state = ST_EOF;
            break;

        case ST_EOF:
            if (byte == PROTO_EOF) {
                uint8_t calc = calc_frame_crc(s_parser.cmd, s_parser.len, s_parser.payload);
                if (calc == s_parser.crc && s_rxCb != NULL) {
                    ProtoFrame_t frame = {
                        .cmd = s_parser.cmd,
                        .len = s_parser.len,
                    };
                    memcpy(frame.payload, s_parser.payload, s_parser.len);
                    s_rxCb(&frame);
                }
            }
            s_parser.state = ST_IDLE;
            break;

        default:
            s_parser.state = ST_IDLE;
            break;
    }
}

bool PROTO_SendFrame(const ProtoFrame_t *frame)
{
    if (frame == NULL || frame->len > PROTO_MAX_PAYLOAD) return false;

    uint16_t pos = 0;
    s_txBuf[pos++] = PROTO_SOF0;
    s_txBuf[pos++] = PROTO_SOF1;
    s_txBuf[pos++] = frame->cmd;
    s_txBuf[pos++] = (uint8_t)(frame->len & 0xFF);
    s_txBuf[pos++] = (uint8_t)((frame->len >> 8) & 0xFF);
    if (frame->len > 0) {
        memcpy(&s_txBuf[pos], frame->payload, frame->len);
        pos += frame->len;
    }
    s_txBuf[pos++] = calc_frame_crc(frame->cmd, frame->len, frame->payload);
    s_txBuf[pos++] = PROTO_EOF;

    return BSP_USB_Transmit(s_txBuf, pos);
}

bool PROTO_SendAck(uint8_t cmd, bool ok)
{
    ProtoFrame_t frame = {
        .cmd = CMD_ACK,
        .len = 2,
    };
    frame.payload[0] = cmd;
    frame.payload[1] = ok ? 0x00 : 0x01;
    return PROTO_SendFrame(&frame);
}
