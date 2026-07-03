#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Direct ADC burst → Excel exporter.  No GUI needed.

Usage:
    python adc_to_excel.py COM7                      # PA6 only, 20kHz, 1000 samples
    python adc_to_excel.py COM7 --ch1                # PA6 + PA7
    python adc_to_excel.py COM7 --ch1 --ch2          # PA6 + PA7 + PC4
    python adc_to_excel.py COM7 --ch1 --rate 100000 --samples 32768
"""

import argparse
import os
import struct
import time
import sys
import serial
import openpyxl
from datetime import datetime

from comm.protocol import (
    build_adc_config, build_adc_burst,
    unpack_frame, CMD_ADC_DATA,
)

ADC_VREF   = 3.3
ADC_MAX    = 4095.0
ADC_OFFSET = 0.055   # voltage correction (V)
CH_PINS    = ["PA6", "PA7", "PC4"]


def main():
    parser = argparse.ArgumentParser(description="ADC burst → Excel")
    parser.add_argument("port", help="COM port, e.g. COM7")
    parser.add_argument("--ch1", action="store_true", help="Enable PA7")
    parser.add_argument("--ch2", action="store_true", help="Enable PC4")
    parser.add_argument("--rate", type=int, default=20000,
                        help="Sample rate Hz (default 20000)")
    parser.add_argument("--samples", type=int, default=1000,
                        help="Samples per channel (default 1000)")
    parser.add_argument("--output", default="",
                        help="Output .xlsx path (default: auto-timestamp)")
    args = parser.parse_args()

    ch_mask = 0x01  # PA6 always on
    if args.ch1:
        ch_mask |= 0x02
    if args.ch2:
        ch_mask |= 0x04
    num_ch = bin(ch_mask).count("1")
    active_pins = [
        CH_PINS[i] for i in range(len(CH_PINS)) if ch_mask & (1 << i)]

    print(f"[INFO] Opening {args.port} ...")
    ser = serial.Serial(args.port, baudrate=115200, timeout=2.0)
    time.sleep(0.5)
    ser.read(ser.in_waiting)  # flush stale

    # ── Send config ────────────────────────────────────────────────
    cfg = build_adc_config(ch_mask, args.rate, mode=0)
    ser.write(cfg.to_bytes())
    time.sleep(0.1)

    # ── Send burst ─────────────────────────────────────────────────
    frame = build_adc_burst(ch_mask, args.samples)
    ser.write(frame.to_bytes())
    print(f"[TX] BURST ch=0x{ch_mask:02X} samples={args.samples} "
          f"rate={args.rate}Hz")

    # ── Receive all CMD_ADC_DATA frames ────────────────────────────
    buf_raw = b""
    burst_total = args.samples * num_ch
    received    = 0
    ch_buffers   = [[] for _ in range(num_ch)]

    print("[RX] Waiting for data frames...")
    while received < burst_total:
        raw = ser.read(4096)
        if not raw:
            if received > 0:
                break  # timeout but we have data
            print("[WARN] Serial timeout — no data received")
            ser.close()
            return 1
        buf_raw += raw

        while True:
            frame, buf_raw = unpack_frame(buf_raw)
            if frame is None:
                break
            if frame.cmd == CMD_ADC_DATA:
                payload = frame.payload
                if len(payload) < 4:
                    continue
                seq  = payload[0] | (payload[1] << 8)
                mask = payload[2]
                raw_bytes = payload[4:]
                samples = struct.unpack(
                    "<" + "H" * (len(raw_bytes) // 2), raw_bytes)
                n = len(samples)
                received += n
                for i in range(0, n, num_ch):
                    for c in range(num_ch):
                        if i + c < len(samples):
                            ch_buffers[c].append(samples[i + c])
                pct = min(100, received * 100 // burst_total)
                print(f"\r[RX] {received}/{burst_total} ({pct}%)  "
                      f"seq={seq}", end="", flush=True)

    print()
    ser.close()

    if received == 0:
        print("[ERROR] No data received.")
        return 1

    # Trim to requested size
    for c in range(num_ch):
        ch_buffers[c] = ch_buffers[c][:args.samples]

    # ── Write Excel ────────────────────────────────────────────────
    out_path = args.output
    if not out_path:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        log_dir = os.path.join(script_dir, "adc_logs")
        os.makedirs(log_dir, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_path = os.path.join(log_dir, f"adc_burst_{ts}.xlsx")

    wb = openpyxl.Workbook()
    ws = wb.active
    ws.title = "ADC数据"

    headers = ["样本序号"] + [f"{p}电压(V)" for p in active_pins]
    for ci, h in enumerate(headers, 1):
        ws.cell(row=1, column=ci, value=h)

    for r in range(args.samples):
        ws.cell(row=r + 2, column=1, value=r)
        for c in range(num_ch):
            ws.cell(row=r + 2, column=c + 2,
                    value=round(ch_buffers[c][r] * ADC_VREF / ADC_MAX
                                - ADC_OFFSET, 4))

    wb.save(out_path)
    print(f"[OK]  Saved: {out_path}  "
          f"({args.samples} samples × {num_ch} channels)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
