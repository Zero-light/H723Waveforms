/*
 * protocol.c
 * Host communication protocol implementation.
 */
#include "protocol.h"
#include "crc8.h"
#include "bsp_usb.h"
#include "stm32h7xx.h"
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

/* Receive frame queue: filled by the USB ISR in PROTO_ParseByte(), drained by
 * the main loop via PROTO_GetFrame().  A dropped oldest entry is used when the
 * host outruns the consumer, matching the host-side serial_link behaviour. */
static ProtoFrame_t s_rxQueue[PROTO_RX_QUEUE_DEPTH];
static volatile uint8_t s_rxHead;
static volatile uint8_t s_rxTail;
static volatile uint8_t s_rxCount;

static void s_rxQueuePush(const ProtoFrame_t *frame)
{
    s_rxQueue[s_rxHead] = *frame;
    s_rxHead = (s_rxHead + 1) % PROTO_RX_QUEUE_DEPTH;
    if (s_rxCount < PROTO_RX_QUEUE_DEPTH) {
        s_rxCount++;
    } else {
        s_rxTail = (s_rxTail + 1) % PROTO_RX_QUEUE_DEPTH; /* drop oldest */
    }
}

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
    s_rxHead = 0;
    s_rxTail = 0;
    s_rxCount = 0;
}

bool PROTO_GetFrame(ProtoFrame_t *frame)
{
    bool got = false;
    if (frame == NULL) {
        return false;
    }
    __disable_irq();
    if (s_rxCount > 0) {
        *frame = s_rxQueue[s_rxTail];
        s_rxTail = (s_rxTail + 1) % PROTO_RX_QUEUE_DEPTH;
        s_rxCount--;
        got = true;
    }
    __enable_irq();
    return got;
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
                if (calc == s_parser.crc) {
                    /* ponytail: static avoids 2 KB stack frame inside USB ISR.
                     * The frame is copied into the ISR-to-mainloop queue; the
                     * callback registered with PROTO_Init is no longer used. */
                    static ProtoFrame_t s_rxFrame;
                    s_rxFrame.cmd = s_parser.cmd;
                    s_rxFrame.len = s_parser.len;
                    memcpy(s_rxFrame.payload, s_parser.payload, s_parser.len);
                    s_rxQueuePush(&s_rxFrame);
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
