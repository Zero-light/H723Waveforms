"""Binary frame protocol codec for STM32H723 Waveforms host."""

import struct
from dataclasses import dataclass
from typing import Optional, Tuple

SOF = bytes([0xA5, 0x5A])
EOF = bytes([0x0A])

CMD_WAVE_CONFIG = 0x01
CMD_WAVE_DATA   = 0x02
CMD_WAVE_CTRL   = 0x03
CMD_ADC_CONFIG  = 0x10
CMD_ADC_CTRL    = 0x11
CMD_ADC_BURST   = 0x13
CMD_ADC_DATA    = 0x12
CMD_SPI_CONFIG  = 0x20
CMD_SPI_XFER    = 0x21
CMD_SPI_RESP    = 0x22
CMD_DAC_SET     = 0x30
CMD_ACK         = 0xF0


@dataclass
class Frame:
    cmd: int
    payload: bytes

    @property
    def len(self) -> int:
        return len(self.payload)

    def to_bytes(self) -> bytes:
        return pack_frame(self)


def _crc8(data: bytes) -> int:
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) if (crc & 0x80) else (crc << 1)
        crc &= 0xFF
    return crc


def pack_frame(frame: Frame) -> bytes:
    """Pack a Frame into raw binary bytes."""
    length = len(frame.payload)
    body = struct.pack("<BH", frame.cmd, length) + frame.payload
    crc = _crc8(body)
    return SOF + body + bytes([crc]) + EOF


def unpack_frame(raw: bytes) -> Tuple[Optional[Frame], bytes]:
    """Attempt to unpack a single Frame from raw bytes.

    Returns the Frame and remaining bytes, or None if incomplete/bad.
    """
    # Simple state machine could be used; here we scan for SOF
    while True:
        idx = raw.find(SOF)
        if idx == -1:
            return None, b""
        raw = raw[idx:]
        # Need at least 5 bytes for SOF + cmd(1) + len(2) + crc(1) + eof(1)
        if len(raw) < 6:
            return None, raw
        cmd = raw[2]
        length = raw[3] | (raw[4] << 8)
        total = 2 + 1 + 2 + length + 1 + 1
        if len(raw) < total:
            return None, raw
        payload = raw[5:5 + length]
        crc_recv = raw[5 + length]
        eof_byte = raw[5 + length + 1]
        if eof_byte != 0x0A:
            raw = raw[2:]  # skip past this SOF and keep searching
            continue
        body = struct.pack("<BH", cmd, length) + payload
        if _crc8(body) != crc_recv:
            raw = raw[2:]
            continue
        return Frame(cmd=cmd, payload=payload), raw[total:]


def build_wave_config(sample_rate_hz: int, num_points: int, ch_mask: int) -> Frame:
    payload = struct.pack("<IHB", sample_rate_hz, num_points, ch_mask)
    return Frame(CMD_WAVE_CONFIG, payload)


def build_wave_data(bsrr_masks: list[int]) -> Frame:
    payload = b"".join(struct.pack("<I", m) for m in bsrr_masks)
    return Frame(CMD_WAVE_DATA, payload)


def build_wave_ctrl(start: bool) -> Frame:
    return Frame(CMD_WAVE_CTRL, bytes([1 if start else 0]))


def build_wave_ctrl_one_shot() -> Frame:
    """One-shot: run one buffer then firmware auto-stops + pulls pins LOW.

    payload[0] = 1  (start)
    payload[1] = 1  (one-shot flag; 0 = loop)
    """
    return Frame(CMD_WAVE_CTRL, bytes([1, 1]))


def build_adc_config(ch_mask: int, sample_rate_hz: int, mode: int = 0) -> Frame:
    payload = struct.pack("<B", ch_mask) + struct.pack("<I", sample_rate_hz) + struct.pack("<B", mode)
    return Frame(CMD_ADC_CONFIG, payload)


def build_adc_ctrl(start: bool) -> Frame:
    return Frame(CMD_ADC_CTRL, bytes([1 if start else 0]))


def build_adc_burst(ch_mask: int, num_samples: int) -> Frame:
    """Request a single-shot burst capture.

    Firmware will configure ADC, capture num_samples, and send back
    as one CMD_ADC_DATA frame.
    """
    payload = struct.pack("<BI", ch_mask, num_samples)
    return Frame(CMD_ADC_BURST, payload)


def parse_adc_data(payload: bytes) -> tuple[int, int, int, bytes]:
    """Parse CMD_ADC_DATA payload.

    Returns (seq_id, ch_mask, mode, raw_sample_bytes).
    Raises ValueError if payload is too short.
    """
    if len(payload) < 4:
        raise ValueError("ADC data payload too short")
    seq_id = payload[0] | (payload[1] << 8)
    ch_mask = payload[2]
    mode = payload[3]
    raw = payload[4:]
    if len(raw) % 2 != 0:
        raise ValueError("ADC sample data length must be even (16-bit samples)")
    return seq_id, ch_mask, mode, raw


def build_spi_config(cpol: int, cpha: int, baud_prescaler: int, frame_bits: int) -> Frame:
    payload = bytes([((cpol & 1) << 1) | (cpha & 1), baud_prescaler, frame_bits])
    return Frame(CMD_SPI_CONFIG, payload)


def build_spi_reg_writes(regs: list[tuple[int, int]], data_bits: int = 8) -> Frame:
    """Build a batch SPI register write frame.

    regs: list of (register_address, register_data) tuples
    data_bits: 8 or 16 (data width per register, little-endian in payload)
    """
    payload = bytes([len(regs), data_bits])
    for addr, data in regs:
        payload += bytes([addr])
        if data_bits == 16:
            payload += struct.pack("<H", data & 0xFFFF)
        else:
            payload += bytes([data & 0xFF])
    return Frame(CMD_SPI_XFER, payload)


def build_dac_set(value: int) -> Frame:
    """Build a DAC output value frame. value: 0~4095 (12-bit)."""
    return Frame(CMD_DAC_SET, struct.pack("<H", value & 0xFFFF))
