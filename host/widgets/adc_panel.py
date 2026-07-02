# -*- coding: utf-8 -*-
"""ADC one-click burst capture + auto Excel export.
Two channels offset vertically, independently adjustable."""

import os
import threading
from datetime import datetime

import numpy as np
import pyqtgraph as pg
import openpyxl
from PyQt6.QtCore import Qt, QTimer, pyqtSignal
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLabel,
    QSpinBox, QDoubleSpinBox, QGroupBox, QMessageBox, QCheckBox,
    QSplitter,
)

from comm.protocol import build_adc_config, build_adc_burst, parse_adc_data
from comm.protocol import CMD_ACK, CMD_ADC_DATA
from comm.serial_link import SerialLink

CH_COLORS = [(255, 80, 80), (80, 200, 80)]
CH_PINS   = ["PA6", "PA7"]
CH_NAMES  = ["PA6", "PA7"]
ADC_VREF  = 3.3
ADC_MAX   = 4095.0
NUM_CH    = 2
CH_BASE   = [5.0, 2.0]   # y-baseline per channel (volt)

class AdcViewBox(pg.ViewBox):
    def __init__(self, *args, **kw):
        super().__init__(*args, **kw)
        self.setMouseMode(pg.ViewBox.PanMode)
    def wheelEvent(self, ev, axis=None):
        if ev.modifiers() & Qt.KeyboardModifier.ControlModifier:
            super().wheelEvent(ev, axis=0)
        else:
            super().wheelEvent(ev, axis=1)
        ev.accept()


class AdcPanel(QWidget):
    log_signal = pyqtSignal(str)

    def __init__(self, link: SerialLink, parent=None):
        super().__init__(parent)
        self.link = link
        self._sample_rate = 20000
        self._lock = threading.Lock()
        self._offset_spins = []

        self._burst_pending = False
        self._burst_expect  = 0
        self._burst_received = 0
        self._burst_num_ch   = 0
        self._burst_ch_mask  = 0
        self._burst_buf      = []
        self._buffers = [[] for _ in range(NUM_CH)]

        self._setup_ui()

        self._plot_timer = QTimer(self)
        self._plot_timer.timeout.connect(self._update_plot)
        self._plot_timer.start(100)

    def _setup_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(4, 4, 4, 4)

        # ── Config row ─────────────────────────────────────────
        cfg = QHBoxLayout()
        cfg.addWidget(QLabel("采样率(Hz):"))
        self.sb_rate = QSpinBox()
        self.sb_rate.setRange(1000, 150000)
        self.sb_rate.setValue(20000)
        self.sb_rate.setSingleStep(1000)
        cfg.addWidget(self.sb_rate)

        cfg.addWidget(QLabel("样本数:"))
        self.sb_samples = QSpinBox()
        self.sb_samples.setRange(100, 32768)
        self.sb_samples.setValue(1000)
        self.sb_samples.setSingleStep(1000)
        cfg.addWidget(self.sb_samples)

        self._ch_checks = []
        for i, pin in enumerate(CH_PINS):
            chk = QCheckBox(f"{pin}")
            chk.setChecked(True)
            self._ch_checks.append(chk)
            cfg.addWidget(chk)

        cfg.addStretch()

        self.btn_go = QPushButton("采集并导出Excel")
        self.btn_go.setStyleSheet("font-weight: bold; min-height: 32px; font-size: 14px;")
        self.btn_go.clicked.connect(self._on_go)
        cfg.addWidget(self.btn_go)
        outer.addLayout(cfg)

        # ── Offset row ────────────────────────────────────────
        off_row = QHBoxLayout()
        off_row.addWidget(QLabel("偏移:"))
        self._offset_spins = []
        for i in range(NUM_CH):
            color = CH_COLORS[i]
            off_row.addWidget(QLabel(f"<font color='rgb{color}'>{CH_NAMES[i]}</font>"))
            spin = QDoubleSpinBox()
            spin.setRange(-3.0, 3.0)
            spin.setSingleStep(0.1)
            spin.setValue(0.0)
            spin.setDecimals(1)
            spin.setFixedWidth(90)
            spin.valueChanged.connect(lambda v, i=i: self._schedule_update())
            off_row.addWidget(spin)
            self._offset_spins.append(spin)
        off_row.addStretch()
        outer.addLayout(off_row)

        self.lbl_status = QLabel("就绪")
        self.lbl_status.setStyleSheet("color: gray;")
        outer.addWidget(self.lbl_status)

        # ── Plot ───────────────────────────────────────────────
        self.plot_widget = pg.PlotWidget(viewBox=AdcViewBox())
        self.plot_widget.setLabel("bottom", "样本序号")
        self.plot_widget.setLabel("left", "电压", units="V")
        self.plot_widget.showGrid(x=True, y=True)
        self.plot_widget.setYRange(-0.5, 7.5, padding=0.0)
        ticks = []
        for i in range(NUM_CH):
            y0 = CH_BASE[i]
            ticks.append((y0, f"{CH_NAMES[i]} 0V"))
            for v in (1, 2, 3):
                ticks.append((y0 + v, f"{CH_NAMES[i]} {v}V"))
        self.plot_widget.getAxis("left").setTicks([ticks])
        self._curves = []
        for i in range(NUM_CH):
            pen = pg.mkPen(color=CH_COLORS[i], width=1.5)
            curve = self.plot_widget.plot(pen=pen, name=CH_NAMES[i])
            self._curves.append(curve)
        outer.addWidget(self.plot_widget, stretch=1)

    def _schedule_update(self):
        self._update_plot()

    def _ch_mask(self):
        mask = 0
        for i, chk in enumerate(self._ch_checks):
            if chk.isChecked():
                mask |= (1 << i)
        return mask

    def _on_go(self):
        if not self.link.is_open():
            QMessageBox.warning(self, "未连接", "请先连接串口。")
            return
        mask = self._ch_mask()
        if mask == 0:
            QMessageBox.warning(self, "通道为空", "至少选择一个通道。")
            return

        rate = self.sb_rate.value()
        ns   = self.sb_samples.value()
        num_ch = bin(mask).count("1")

        # Clear previous
        with self._lock:
            for b in self._buffers:
                b.clear()

        self._burst_pending  = True
        self._burst_expect   = ns * num_ch
        self._burst_received = 0
        self._burst_num_ch   = num_ch
        self._burst_ch_mask  = mask
        self._burst_buf      = [[] for _ in range(num_ch)]

        self.btn_go.setEnabled(False)
        self.lbl_status.setText(f"采集中... {ns}样本 x {num_ch}通道 @ {rate}Hz")
        self.lbl_status.setStyleSheet("color: orange; font-weight: bold;")

        self.link.send(build_adc_config(mask, rate, mode=0).to_bytes())
        QTimer.singleShot(50, lambda: self._send_burst(mask, ns))

    def _send_burst(self, mask, ns):
        self.link.send(build_adc_burst(mask, ns).to_bytes())
        self.log_signal.emit(f"[TX] BURST ch=0x{mask:02X} n={ns} rate={self.sb_rate.value()}Hz")

    def on_frame(self, frame):
        if frame.cmd == CMD_ADC_DATA:
            try:
                _, ch_mask, _, raw = parse_adc_data(frame.payload)
            except ValueError:
                return
            samples = np.frombuffer(raw, dtype=np.uint16)
            if len(samples) == 0:
                return
            num_en = bin(ch_mask).count("1")
            if num_en == 0:
                return
            scan_periods = len(samples) // num_en
            if scan_periods == 0:
                return
            samples = samples[:scan_periods * num_en]
            reshaped = samples.reshape(scan_periods, num_en)

            if self._burst_pending and ch_mask == self._burst_ch_mask:
                buf = self._burst_buf
                for c in range(num_en):
                    buf[c].append(reshaped[:, c].copy())
                self._burst_received += scan_periods
                expected_spc = self._burst_expect // num_en

                if self._burst_received >= expected_spc:
                    with self._lock:
                        for c in range(num_en):
                            self._buffers[c] = list(np.concatenate(buf[c])) if buf[c] else []
                        for c in range(num_en, NUM_CH):
                            self._buffers[c] = []

                    self._burst_pending = False
                    self._update_plot()
                    self._export_excel()
                    self.btn_go.setEnabled(True)
                    self.lbl_status.setText(f"完成: {expected_spc} 样本/通道")
                    self.lbl_status.setStyleSheet("color: green; font-weight: bold;")
                    self.log_signal.emit(f"[INFO] Burst完成: {expected_spc} spc/ch")

        elif frame.cmd == CMD_ACK:
            pass

    def _export_excel(self):
        log_dir = r"D:\test\STM32H723ZGT6\host\adc_logs"
        os.makedirs(log_dir, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = os.path.join(log_dir, f"adc_{ts}.xlsx")

        wb = openpyxl.Workbook()
        ws = wb.active
        ws.title = "ADC"
        num_ch = self._burst_num_ch
        headers = ["样本序号"]
        for c in range(num_ch):
            headers.append(f"{CH_PINS[c]} 电压(V)")
        for ci, h in enumerate(headers, 1):
            ws.cell(row=1, column=ci, value=h)

        spc = len(self._buffers[0]) if self._buffers[0] else 0
        for r in range(spc):
            ws.cell(row=r + 2, column=1, value=r)
            for c in range(num_ch):
                raw = self._buffers[c][r] if r < len(self._buffers[c]) else 0
                ws.cell(row=r + 2, column=c + 2,
                        value=round(raw * ADC_VREF / ADC_MAX, 4))
        wb.save(path)
        self.log_signal.emit(f"[INFO] Excel已保存: {path}")

    def _update_plot(self):
        with self._lock:
            bufs = [list(b) for b in self._buffers]
        for i in range(NUM_CH):
            buf = bufs[i]
            if not buf or i >= self._burst_num_ch:
                self._curves[i].setData([], [])
                continue
            arr = np.array(buf, dtype=np.float32)
            volts = arr * ADC_VREF / ADC_MAX
            n = len(volts)
            offset = self._offset_spins[i].value() if i < len(self._offset_spins) else 0.0
            y_base = CH_BASE[i] + offset

            if n > 8000:
                bs = max(n // 4000, 2)
                trim = n - (n % bs)
                v = volts[:trim].reshape(-1, bs)
                v_max = v.max(axis=1)
                v_min = v.min(axis=1)
                x = np.arange(0, trim, bs, dtype=np.float64)
                x2 = np.empty(len(v_max) * 2, dtype=np.float64)
                y2 = np.empty(len(v_max) * 2, dtype=np.float32)
                x2[0::2] = x; x2[1::2] = x + bs
                y2[0::2] = v_min + y_base; y2[1::2] = v_max + y_base
                self._curves[i].setData(x2, y2)
            else:
                self._curves[i].setData(np.arange(n), volts + y_base)
