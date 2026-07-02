#!/usr/bin/env python3
"""Test SPI batch register writes via USB CDC."""

import serial
import time

from comm.protocol import build_spi_config, build_spi_reg_writes

PORT = "COM7"
BAUD = 115200


def main():
    print(f"Opening {PORT}...")
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1.0, write_timeout=1.0)
    except Exception as e:
        print(f"Failed: {e}")
        return

    time.sleep(1.0)
    stale = ser.read(ser.in_waiting)
    if stale:
        print(f"Stale data: {stale.hex()}")

    # --- Configure SPI: CPOL=0, CPHA=0, /64 = 1 MHz, 8-bit ---
    frame = build_spi_config(0, 0, 5, 8)
    ser.write(frame.to_bytes())
    time.sleep(0.2)
    rx = ser.read(ser.in_waiting)
    print(f"Config ACK: {rx.hex()}")

    # --- Batch write 8-bit registers ---
    regs_8 = [
        (0x0E, 0x83),
        (0x10, 0x01),
        (0x20, 0x55),
    ]
    frame = build_spi_reg_writes(regs_8, data_bits=8)
    ser.write(frame.to_bytes())
    time.sleep(0.2)
    rx = ser.read(ser.in_waiting)
    print(f"Write8 ACK: {rx.hex()}")

    # --- Reconfigure for 16-bit data ---
    frame = build_spi_config(0, 0, 5, 16)
    ser.write(frame.to_bytes())
    time.sleep(0.2)
    rx = ser.read(ser.in_waiting)
    print(f"Config16 ACK: {rx.hex()}")

    # --- Batch write 16-bit registers ---
    regs_16 = [
        (0x0E, 0x0083),
        (0x10, 0x1234),
    ]
    frame = build_spi_reg_writes(regs_16, data_bits=16)
    ser.write(frame.to_bytes())
    time.sleep(0.2)
    rx = ser.read(ser.in_waiting)
    print(f"Write16 ACK: {rx.hex()}")

    ser.close()
    print("Done.")


if __name__ == "__main__":
    main()
