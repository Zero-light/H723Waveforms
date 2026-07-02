#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Standardized firmware flash script.

Cross-board safety: the script refuses to flash a file whose path does not
contain the requested board ID, preventing accidental flashing of the wrong
hardware.

Examples:
    python flash.py --board h723zg_v1 --file ../firmware/output/h723zg_v1/debug/firmware.hex
    python flash.py --board h723zg_v1 --profile debug
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CUBE_PROG = os.path.join(ROOT, "cubeMX", "Cubeprogrammer", "bin", "STM32_Programmer_CLI.exe")


def find_hex(board: str, profile: str) -> str:
    path = os.path.join(ROOT, "firmware", "output", board, profile, "firmware.hex")
    if not os.path.exists(path):
        raise FileNotFoundError(f"Firmware not found: {path}")
    return path


def flash(board: str, hex_file: str, interface: str = "swd") -> int:
    board_in_path = os.path.normpath(hex_file).split(os.sep)
    if board not in board_in_path:
        print(f"[ERROR] Safety check failed: '{hex_file}' does not contain board '{board}'")
        print("[ERROR] Refusing to flash possibly incompatible firmware.")
        return 1

    if not os.path.exists(CUBE_PROG):
        print(f"[ERROR] CubeProgrammer not found at {CUBE_PROG}")
        print("[INFO] Install STM32CubeProgrammer or update CUBE_PROG path.")
        return 1

    cmd = [
        CUBE_PROG,
        "--connect",
        f"port={interface}",
        "--download",
        hex_file,
        "--start",
    ]
    print(f"[INFO] Flashing {hex_file} to board {board} via {interface}")
    result = subprocess.run(cmd)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Flash firmware to target board")
    parser.add_argument("--board", required=True, help="Board ID, e.g. h723zg_v1")
    parser.add_argument("--file", help="Path to .hex file")
    parser.add_argument("--profile", default="debug", help="Build profile when --file is omitted")
    parser.add_argument("--interface", default="swd", help="Debug interface")
    args = parser.parse_args()

    hex_file = args.file or find_hex(args.board, args.profile)
    return flash(args.board, hex_file, args.interface)


if __name__ == "__main__":
    sys.exit(main())
