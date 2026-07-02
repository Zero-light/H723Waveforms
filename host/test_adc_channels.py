#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Minimal ADC channel verifier for STM32H723 Waveforms firmware.

Connects via USB CDC, configures ADC for PA6 (always enabled) and optional
PA7, starts acquisition, and prints RAW values.

Usage:
    python test_adc_channels.py COM3          # PA6 only
    python test_adc_channels.py COM3 --pa7    # PA6 + PA7

Expected RAW values @ 3.3V Vref, 12-bit:
    GND  -> ~0
    3.3V -> ~4095
    1.5V -> ~1860
    open -> floating/noise (do not use for verification)
"""

import argparse
import struct
import serial
import sys
from comm.protocol import (
    build_adc_config,
    build_adc_ctrl,
    unpack_frame,
    CMD_ADC_DATA,
)


def main() -> int:
    parser = argparse.ArgumentParser(description="ADC RAW verifier (PA6 + optional PA7)")
    parser.add_argument("port", help="COM port, e.g. COM3")
    parser.add_argument("--pa7", action="store_true", help="Also enable PA7")
    args = parser.parse_args()

    ser = serial.Serial(args.port, baudrate=115200, timeout=0.5)
    print(f"[INFO] Opened {args.port}")

    # PA6 is always enabled; PA7 is optional
    ch_mask = 0x01 | (0x02 if args.pa7 else 0x00)
    cfg = build_adc_config(ch_mask=ch_mask, sample_rate_hz=1000, mode=0)
    ser.write(cfg.to_bytes())
    print(f"[TX] ADC_CONFIG ch_mask=0x{ch_mask:02X} rate=1000Hz")

    ser.write(build_adc_ctrl(True).to_bytes())
    print("[TX] ADC_CTRL start")

    print("\n[INFO] Reading RAW 12-bit samples. Press Ctrl+C to stop.\n")
    header = f"{'seq_id':>8} | {'PA6 (CH0)':>10}"
    if args.pa7:
        header += " | {'PA7 (CH1)':>10}"
    header += " | voltage_CH0"
    if args.pa7:
        header += " voltage_CH1"
    print(header)

    buf = b""
    try:
        while True:
            raw = ser.read(256)
            if raw:
                # Print any ASCII debug text (like [ADC DBG]) before frame parsing
                text = ''.join(chr(b) if 32 <= b < 127 or b in (10,13) else '' for b in raw)
                if text.strip():
                    print(text, end='', flush=True)
                buf += raw
            frame, buf = unpack_frame(buf)
            if frame is None:
                continue
            if frame.cmd == CMD_ADC_DATA:
                payload = frame.payload
                if len(payload) < 4:
                    continue
                seq_id = payload[0] | (payload[1] << 8)
                ch_mask = payload[2]
                samples = struct.unpack("<" + "H" * ((len(payload) - 4) // 2), payload[4:])
                num_ch = bin(ch_mask).count("1")
                if num_ch == 0 or len(samples) < num_ch:
                    continue
                last = samples[-num_ch:]
                raw0 = last[0]
                v0 = raw0 * 3.3 / 4095.0
                line = f"{seq_id:>8} | {raw0:>10}"
                if args.pa7 and num_ch > 1:
                    raw1 = last[1]
                    v1 = raw1 * 3.3 / 4095.0
                    line += f" | {raw1:>10}"
                    line += f" | {v0:.3f}V {v1:.3f}V"
                else:
                    line += f" | {v0:.3f}V"
                print(line)
    except KeyboardInterrupt:
        print("\n[INFO] Stopping ADC...")
        ser.write(build_adc_ctrl(False).to_bytes())
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
