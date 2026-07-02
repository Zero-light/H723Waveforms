#!/usr/bin/env python3
"""Quick comm test: send raw bytes and see if MCU echoes back."""

import serial
import time
import struct

PORT = "COM7"
BAUD = 115200


def crc8(data: bytes) -> int:
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) if (crc & 0x80) else (crc << 1)
        crc &= 0xFF
    return crc


def build_frame(cmd: int, payload: bytes) -> bytes:
    body = struct.pack("<BH", cmd, len(payload)) + payload
    crc = crc8(body)
    return b"\xA5\x5A" + body + bytes([crc]) + b"\x0A"


def main():
    print(f"Opening {PORT}...")
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1.0, write_timeout=1.0)
    except Exception as e:
        print(f"Failed to open port: {e}")
        return

    print("Port open. Waiting 1s for MCU to settle...")
    time.sleep(1.0)

    # Flush any stale data
    stale = ser.read(ser.in_waiting)
    if stale:
        print(f"Stale rx data: {stale.hex()}")

    # Test 1: Send WAVE_CONFIG frame
    payload = struct.pack("<IHB", 1000, 8, 31)
    frame = build_frame(0x01, payload)
    print(f"\n[TX] WAVE_CONFIG frame ({len(frame)} bytes): {frame.hex()}")
    ser.write(frame)

    time.sleep(0.3)
    rx = ser.read(ser.in_waiting)
    print(f"[RX] {len(rx)} bytes: {rx.hex() if rx else '(nothing)'}")

    # Test 2: Send WAVE_CTRL START
    frame2 = build_frame(0x03, b"\x01")
    print(f"\n[TX] WAVE_CTRL START frame ({len(frame2)} bytes): {frame2.hex()}")
    ser.write(frame2)

    time.sleep(0.3)
    rx2 = ser.read(ser.in_waiting)
    print(f"[RX] {len(rx2)} bytes: {rx2.hex() if rx2 else '(nothing)'}")

    # Test 3: Send some raw ASCII to see if MCU replies at all
    print(f"\n[TX] Raw ASCII 'hello\\n'")
    ser.write(b"hello\n")

    time.sleep(0.3)
    rx3 = ser.read(ser.in_waiting)
    print(f"[RX] {len(rx3)} bytes: {rx3.hex() if rx3 else '(nothing)'}")
    if rx3:
        try:
            print(f"     as text: {rx3.decode('ascii', errors='replace')}")
        except:
            pass

    ser.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
