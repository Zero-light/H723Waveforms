#include "app_protocol.h"
#include "usbd_cdc_if.h"
#include <string.h>

/* Reuse a small static tx buffer; caller should finish quickly */
static uint8_t s_txBuf[FRAME_MAX_PAYLOAD + 8];
static FrameRxCallback_t s_rxCb = NULL;
/* Single-slot pending frame — filled from ISR, consumed in main loop */
static struct {
    Frame_t frame;
    volatile bool pending;
} s_pendingRx;

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
    uint8_t  payload[FRAME_MAX_PAYLOAD];
    uint8_t  crc;
} s_parser;

/* CRC-8 / SMBus polynomial 0x07 */
static uint8_t crc8_update(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t b = 0; b < 8; b++) {
        crc = (crc & 0x80u) ? ((crc << 1) ^ 0x07u) : (crc << 1);
    }
    return crc;
}

uint8_t APP_Protocol_CRC8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++) {
        crc = crc8_update(crc, data[i]);
    }
    return crc;
}

void APP_Protocol_Init(FrameRxCallback_t cb)
{
    s_rxCb = cb;
    memset(&s_parser, 0, sizeof(s_parser));
    s_parser.state = ST_IDLE;
}

void APP_Protocol_ParseByte(uint8_t byte)
{
    switch (s_parser.state) {
        case ST_IDLE:
            if (byte == FRAME_SOF0) s_parser.state = ST_SOF0;
            break;

        case ST_SOF0:
            s_parser.state = (byte == FRAME_SOF1) ? ST_CMD : ST_IDLE;
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
            if (s_parser.len > FRAME_MAX_PAYLOAD) {
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
            if (byte == FRAME_EOF) {
                uint8_t calc = 0x00;
                calc = crc8_update(calc, s_parser.cmd);
                calc = crc8_update(calc, (uint8_t)(s_parser.len & 0xFF));
                calc = crc8_update(calc, (uint8_t)((s_parser.len >> 8) & 0xFF));
                for (uint16_t i = 0; i < s_parser.len; i++) {
                    calc = crc8_update(calc, s_parser.payload[i]);
                }
                if (calc == s_parser.crc) {
                    /* Store frame for main-loop processing (ISR-safe, single-slot) */
                    if (!s_pendingRx.pending) {
                        s_pendingRx.frame.cmd = s_parser.cmd;
                        s_pendingRx.frame.len = s_parser.len;
                        memcpy(s_pendingRx.frame.payload, s_parser.payload, s_parser.len);
                        s_pendingRx.pending = true;
                    }
                }
            }
            s_parser.state = ST_IDLE;
            break;

        default:
            s_parser.state = ST_IDLE;
            break;
    }
}

bool APP_Protocol_SendFrame(const Frame_t *frame)
{
    if (frame == NULL || frame->len > FRAME_MAX_PAYLOAD) return false;

    /* Critical section: s_txBuf is shared between main loop and ISR callbacks */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint16_t pos = 0;
    s_txBuf[pos++] = FRAME_SOF0;
    s_txBuf[pos++] = FRAME_SOF1;
    s_txBuf[pos++] = frame->cmd;
    s_txBuf[pos++] = (uint8_t)(frame->len & 0xFF);
    s_txBuf[pos++] = (uint8_t)((frame->len >> 8) & 0xFF);
    if (frame->len > 0) {
        memcpy(&s_txBuf[pos], frame->payload, frame->len);
        pos += frame->len;
    }
    uint8_t crc = 0x00;
    crc = crc8_update(crc, frame->cmd);
    crc = crc8_update(crc, (uint8_t)(frame->len & 0xFF));
    crc = crc8_update(crc, (uint8_t)((frame->len >> 8) & 0xFF));
    for (uint16_t i = 0; i < frame->len; i++) {
        crc = crc8_update(crc, frame->payload[i]);
    }
    s_txBuf[pos++] = crc;
    s_txBuf[pos++] = FRAME_EOF;

    /* CDC_Transmit_HS is non-blocking on H7 */
    uint8_t ret = CDC_Transmit_HS(s_txBuf, pos);

    if (!primask) {
        __enable_irq();
    }
    return (ret == USBD_OK);
}

bool APP_Protocol_SendAck(uint8_t cmd, bool ok)
{
    Frame_t frame = {
        .cmd = CMD_ACK,
        .len = 2,
    };
    frame.payload[0] = cmd;
    frame.payload[1] = ok ? 0x00 : 0x01;
    return APP_Protocol_SendFrame(&frame);
}

bool APP_Protocol_GetPendingFrame(Frame_t *out)
{
    if (!s_pendingRx.pending || out == NULL) return false;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    memcpy(out, &s_pendingRx.frame, sizeof(Frame_t));
    s_pendingRx.pending = false;

    if (!primask) __enable_irq();
    return true;
}
