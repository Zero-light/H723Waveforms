"""DAC voltage control panel."""

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLabel,
    QSlider, QDoubleSpinBox, QGroupBox, QMessageBox,
)
from PyQt6.QtCore import Qt, pyqtSignal

from comm.protocol import build_dac_set, CMD_ACK, CMD_DAC_SET
from comm.serial_link import SerialLink

DAC_MAX = 4095
DAC_VREF = 3.3


class DacPanel(QWidget):
    log_signal = pyqtSignal(str)

    def __init__(self, link: SerialLink, parent=None):
        super().__init__(parent)
        self.link = link
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # --- Pin info ---
        pin_box = QGroupBox("引脚映射")
        pin_layout = QHBoxLayout(pin_box)
        pin_layout.addWidget(QLabel("<b>PA4</b> = DAC1_OUT1"))
        pin_layout.addStretch()
        layout.addWidget(pin_box)

        # --- Voltage control ---
        ctrl_box = QGroupBox("DAC 输出控制")
        ctrl_layout = QVBoxLayout(ctrl_box)

        slider_layout = QHBoxLayout()
        slider_layout.addWidget(QLabel("电压:"))

        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.slider.setRange(0, DAC_MAX)
        self.slider.setValue(0)
        self.slider.valueChanged.connect(self._on_slider_changed)
        slider_layout.addWidget(self.slider)

        self.sb_volt = QDoubleSpinBox()
        self.sb_volt.setRange(0.0, DAC_VREF)
        self.sb_volt.setDecimals(3)
        self.sb_volt.setSingleStep(0.001)
        self.sb_volt.setSuffix(" V")
        self.sb_volt.setValue(0.0)
        self.sb_volt.valueChanged.connect(self._on_volt_changed)
        slider_layout.addWidget(self.sb_volt)

        ctrl_layout.addLayout(slider_layout)

        self.lbl_code = QLabel("DAC 码值: 0 / 4095")
        ctrl_layout.addWidget(self.lbl_code)

        self.btn_send = QPushButton("发送")
        self.btn_send.clicked.connect(self._send_dac)
        ctrl_layout.addWidget(self.btn_send)

        layout.addWidget(ctrl_box)
        layout.addStretch()

    def _on_slider_changed(self, value: int):
        volt = value * DAC_VREF / DAC_MAX
        self.sb_volt.blockSignals(True)
        self.sb_volt.setValue(volt)
        self.sb_volt.blockSignals(False)
        self.lbl_code.setText(f"DAC 码值: {value} / {DAC_MAX}")

    def _on_volt_changed(self, volt: float):
        value = int(round(volt * DAC_MAX / DAC_VREF))
        value = max(0, min(DAC_MAX, value))
        self.slider.blockSignals(True)
        self.slider.setValue(value)
        self.slider.blockSignals(False)
        self.lbl_code.setText(f"DAC 码值: {value} / {DAC_MAX}")

    def _send_dac(self):
        if not self.link.is_open():
            QMessageBox.warning(self, "未连接", "串口未打开。")
            return
        value = self.slider.value()
        frame = build_dac_set(value)
        self.link.send(frame.to_bytes())
        self.log_signal.emit(f"[TX] DAC设置 码值={value} 电压={value * DAC_VREF / DAC_MAX:.3f}V")

    def on_frame(self, frame):
        if frame.cmd == CMD_ACK:
            if len(frame.payload) < 2:
                return
            if frame.payload[0] == CMD_DAC_SET:
                ok = "成功" if frame.payload[1] == 0 else "失败"
                self.log_signal.emit(f"[RX] 确认 DAC_SET {ok}")
