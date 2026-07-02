"""Main application window."""

import os
import sys
from datetime import datetime

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QComboBox, QPushButton, QLabel, QTabWidget,
    QPlainTextEdit, QStatusBar, QSizePolicy, QSplitter,
)
from PyQt6.QtCore import Qt, pyqtSlot, pyqtSignal

from comm.serial_link import SerialLink
from comm.protocol import CMD_ACK
from widgets.wave_panel import WavePanel
from widgets.spi_panel import SpiPanel
from widgets.adc_panel import AdcPanel
from widgets.dac_panel import DacPanel


class MainWindow(QMainWindow):
    log_signal = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("STM32H723 波形发生器")
        self.setMinimumSize(900, 600)

        self.link = SerialLink(on_frame=self._on_frame)
        self.log_signal.connect(self._log)

        # --- Log file setup ---
        if getattr(sys, 'frozen', False):
            base_dir = os.path.dirname(sys.executable)
        else:
            base_dir = os.path.dirname(os.path.abspath(__file__))
        log_dir = os.path.join(base_dir, "logs")
        os.makedirs(log_dir, exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._log_path = os.path.join(log_dir, f"h723_{timestamp}.log")
        self._log_file = open(self._log_path, "w", encoding="utf-8")

        self._setup_ui()
        self._refresh_ports()

    def _setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        # --- Connection bar ---
        conn_layout = QHBoxLayout()
        conn_layout.addWidget(QLabel("串口:"))
        self.cb_port = QComboBox()
        self.cb_port.setMinimumWidth(150)
        conn_layout.addWidget(self.cb_port)

        self.btn_refresh = QPushButton("🔄 刷新")
        self.btn_refresh.clicked.connect(self._refresh_ports)
        conn_layout.addWidget(self.btn_refresh)

        self.btn_connect = QPushButton("连接")
        self.btn_connect.setCheckable(True)
        self.btn_connect.clicked.connect(self._toggle_connect)
        conn_layout.addWidget(self.btn_connect)

        conn_layout.addStretch()
        layout.addLayout(conn_layout)

        # --- Tabs ---
        self.tabs = QTabWidget()

        self.wave_panel = WavePanel(self.link)
        self.wave_panel.log_signal.connect(self._log)
        self.tabs.addTab(self.wave_panel, "波形")

        self.spi_panel = SpiPanel(self.link)
        self.spi_panel.log_signal.connect(self._log)
        self.tabs.addTab(self.spi_panel, "SPI")

        self.adc_panel = AdcPanel(self.link)
        self.adc_panel.log_signal.connect(self._log)
        self.tabs.addTab(self.adc_panel, "ADC")

        self.dac_panel = DacPanel(self.link)
        self.dac_panel.log_signal.connect(self._log)
        self.tabs.addTab(self.dac_panel, "DAC")

        # --- Log console ---
        self.log_edit = QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        self.log_edit.setMaximumBlockCount(500)
        self.log_edit.setPlaceholderText("通信日志...")
        self.log_edit.setMinimumHeight(40)
        self.log_edit.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        # Use QSplitter so user can freely resize tabs vs log area
        splitter = QSplitter(Qt.Orientation.Vertical)
        splitter.addWidget(self.tabs)
        splitter.addWidget(self.log_edit)
        splitter.setSizes([520, 80])
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 0)
        splitter.setCollapsible(1, True)
        layout.addWidget(splitter, stretch=1)

        self.status = QStatusBar()
        self.setStatusBar(self.status)
        self.status.showMessage(f"就绪 | 日志: {os.path.basename(self._log_path)}")

    def _refresh_ports(self):
        import serial.tools.list_ports
        self.cb_port.clear()
        ports = serial.tools.list_ports.comports()
        for p in ports:
            self.cb_port.addItem(f"{p.device} - {p.description}", p.device)
        if self.cb_port.count() == 0:
            self.cb_port.addItem("未找到串口")

    def _toggle_connect(self):
        if self.btn_connect.isChecked():
            port = self.cb_port.currentData()
            if not port:
                self.btn_connect.setChecked(False)
                return
            ok = self.link.open(port)
            if ok:
                self.btn_connect.setText("断开")
                self.status.showMessage(f"已连接到 {port}")
                self._log(f"已连接到 {port}")
            else:
                self.btn_connect.setChecked(False)
                self.status.showMessage("连接失败")
        else:
            self.link.close()
            self.btn_connect.setText("连接")
            self.status.showMessage("已断开")
            self._log("已断开")

    @pyqtSlot(str)
    def _log(self, text: str):
        self.log_edit.appendPlainText(text)
        if hasattr(self, '_log_file') and self._log_file:
            self._log_file.write(text + "\n")
            self._log_file.flush()

    def _on_frame(self, frame):
        # Route frame to all panels (they filter by ACK/cmd themselves)
        self.wave_panel.on_frame(frame)
        self.spi_panel.on_frame(frame)
        self.adc_panel.on_frame(frame)
        self.dac_panel.on_frame(frame)

        if frame.cmd == CMD_ACK:
            self.log_signal.emit("[RX] 确认")
        elif frame.cmd == 0xFF:
            self.log_signal.emit(f"[RX] 心跳: {frame.payload.decode('latin-1')}")
        else:
            self.log_signal.emit(f"[RX] 命令=0x{frame.cmd:02X} 长度={len(frame.payload)}")

    def closeEvent(self, event):
        self.link.close()
        event.accept()
