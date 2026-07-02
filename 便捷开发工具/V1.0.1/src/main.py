#!/usr/bin/env python3
"""Entry point for STM32H723 Waveforms host application."""

import sys
from PyQt6.QtWidgets import QApplication
from main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("H723Waveforms")
    app.setApplicationDisplayName("STM32H723 Waveforms")

    window = MainWindow()
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
