# -*- coding: utf-8 -*-
"""ADC multi-channel burst capture — 3 linked X axes, independent Y axes."""

import json
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

CH_COLORS  = [(255, 80, 80), (80, 200, 80), (80, 120, 255)]  # red, green, blue
CH_PINS    = ["PA6", "PA7", "PC4"]
CH_NAMES   = ["PA6", "PA7", "PC4"]
ADC_VREF   = 3.3
ADC_MAX    = 4095.0
NUM_CH     = 3
_CONFIG_PATH = os.path.join(os.path.expanduser("~"), ".h723_adc_config.json")


class AdcViewBox(pg.ViewBox):
    """Pan horizontally; Ctrl+wheel = horiz zoom, wheel = vert zoom."""
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
        self._adc_offset = 0.0   # loaded from config
        self._lock = threading.Lock()
        self._offset_spins = []

        self._burst_pending  = False
        self._burst_expect   = 0
        self._burst_received = 0
        self._burst_num_ch   = 0
        self._burst_ch_mask  = 0
        self._burst_ch_map   = []   # col_index → physical ch index
        self._burst_buf      = []
        self._buffers = [[] for _ in range(NUM_CH)]

        self._setup_ui()
        self._load_config()

        self._plot_timer = QTimer(self)
        self._plot_timer.timeout.connect(self._update_plot)
        self._plot_timer.start(100)

    # ----------------------------------------------------------------
    # Config persistence
    # ----------------------------------------------------------------

    def _load_config(self):
        try:
            if os.path.exists(_CONFIG_PATH):
                with open(_CONFIG_PATH, "r", encoding="utf-8") as f:
                    cfg = json.load(f)
                self._adc_offset = float(cfg.get("adc_offset_v", 0.0))
                self.sb_adc_offset.blockSignals(True)
                self.sb_adc_offset.setValue(self._adc_offset)
                self.sb_adc_offset.blockSignals(False)
        except Exception:
            self._adc_offset = 0.0

    def _save_offset(self):
        try:
            cfg = {"adc_offset_v": self._adc_offset}
            with open(_CONFIG_PATH, "w", encoding="utf-8") as f:
                json.dump(cfg, f, ensure_ascii=False, indent=2)
            self.log_signal.emit(
                f"[INFO] ADC修正已保存: {self._adc_offset:+.3f}V")
        except Exception as e:
            QMessageBox.warning(self, "保存失败", str(e))

    def _setup_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(4, 4, 4, 4)

        # ── Config row ─────────────────────────────────────────
        cfg = QHBoxLayout()
        cfg.addWidget(QLabel("采样率(Hz):"))
        self.sb_rate = QSpinBox()
        self.sb_rate.setRange(1000, 1000000)
        self.sb_rate.setValue(20000)
        self.sb_rate.setSingleStep(1000)
        cfg.addWidget(self.sb_rate)

        cfg.addWidget(QLabel("样本数:"))
        self.sb_samples = QSpinBox()
        self.sb_samples.setRange(100, 16384)
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
        self.btn_go.setStyleSheet(
            "font-weight: bold; min-height: 32px; font-size: 14px;")
        self.btn_go.clicked.connect(self._on_go)
        cfg.addWidget(self.btn_go)
        outer.addLayout(cfg)

        # ── ADC 电压修正 ─────────────────────────────────────
        corr_row = QHBoxLayout()
        corr_row.addWidget(QLabel("ADC修正(V):"))
        self.sb_adc_offset = QDoubleSpinBox()
        self.sb_adc_offset.setRange(-1.0, 1.0)
        self.sb_adc_offset.setSingleStep(0.001)
        self.sb_adc_offset.setDecimals(3)
        self.sb_adc_offset.setValue(self._adc_offset)
        self.sb_adc_offset.setFixedWidth(100)
        self.sb_adc_offset.valueChanged.connect(lambda v: setattr(self, '_adc_offset', v))
        corr_row.addWidget(self.sb_adc_offset)
        self.btn_save_offset = QPushButton("保存修正")
        self.btn_save_offset.clicked.connect(self._save_offset)
        corr_row.addWidget(self.btn_save_offset)
        corr_row.addStretch()
        outer.addLayout(corr_row)

        # ── Offset row ────────────────────────────────────────
        off_row = QHBoxLayout()
        off_row.addWidget(QLabel("偏移(V):"))
        self._offset_spins = []
        for i in range(NUM_CH):
            color = CH_COLORS[i]
            off_row.addWidget(QLabel(
                f"<font color='rgb{color}'>{CH_NAMES[i]}</font>"))
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

        # ── 3 linked plots (stacked vertically, X-linked) ─────
        self._plots  = []
        self._curves = []

        splitter = QSplitter(Qt.Orientation.Vertical)

        for i in range(NUM_CH):
            pw = pg.PlotWidget(viewBox=AdcViewBox())
            pw.setLabel("left", CH_NAMES[i], units="V")
            pw.setYRange(0, 4, padding=0)
            pw.showGrid(x=True, y=True)

            pen = pg.mkPen(color=CH_COLORS[i], width=1.5)
            curve = pw.plot(pen=pen, name=CH_NAMES[i])

            self._plots.append(pw)
            self._curves.append(curve)
            splitter.addWidget(pw)

        # Link X-axes to the first plot
        base_vb = self._plots[0].getViewBox()
        for i in range(1, NUM_CH):
            self._plots[i].getViewBox().setXLink(base_vb)

        # Only bottom plot shows X-axis tick labels
        self._plots[-1].setLabel("bottom", "样本序号")
        for i in range(NUM_CH - 1):
            self._plots[i].getAxis("bottom").setStyle(showValues=False)

        splitter.setSizes([150, 150, 150])
        outer.addWidget(splitter, stretch=1)

    # ----------------------------------------------------------------
    # Helpers
    # ----------------------------------------------------------------

    def _schedule_update(self):
        self._update_plot()

    def _ch_mask(self):
        mask = 0
        for i, chk in enumerate(self._ch_checks):
            if chk.isChecked():
                mask |= (1 << i)
        return mask

    # ----------------------------------------------------------------
    # Burst capture
    # ----------------------------------------------------------------

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

        # Build column→physical mapping  (firmware sends in PA6→PA7→PC4 order)
        ch_map = [i for i in range(NUM_CH) if mask & (1 << i)]

        with self._lock:
            for b in self._buffers:
                b.clear()

        self._burst_pending  = True
        self._burst_expect   = ns * num_ch
        self._burst_received = 0
        self._burst_num_ch   = num_ch
        self._burst_ch_mask  = mask
        self._burst_ch_map   = ch_map
        self._burst_buf      = [[] for _ in range(num_ch)]

        self.btn_go.setEnabled(False)
        ch_names = ", ".join(
            CH_PINS[i] for i in range(NUM_CH) if mask & (1 << i))
        self.lbl_status.setText(
            f"采集中... {ns}样本 x {num_ch}通道({ch_names}) @ {rate}Hz")
        self.lbl_status.setStyleSheet("color: orange; font-weight: bold;")

        self.link.send(build_adc_config(mask, rate, mode=0).to_bytes())
        QTimer.singleShot(50, lambda: self._send_burst(mask, ns))

    def _send_burst(self, mask, ns):
        self.link.send(build_adc_burst(mask, ns).to_bytes())
        self.log_signal.emit(
            f"[TX] BURST ch=0x{mask:02X} n={ns} "
            f"rate={self.sb_rate.value()}Hz")

    # ----------------------------------------------------------------
    # Frame handler
    # ----------------------------------------------------------------

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
                    ch_map = self._burst_ch_map
                    with self._lock:
                        for col in range(num_en):
                            phys = ch_map[col]
                            self._buffers[phys] = (
                                list(np.concatenate(buf[col]))
                                if buf[col] else [])
                        # Clear unused channels
                        for i in range(NUM_CH):
                            if i not in ch_map:
                                self._buffers[i] = []

                    self._burst_pending = False
                    self._update_plot()
                    self._export_excel()
                    self.btn_go.setEnabled(True)
                    self.lbl_status.setText(f"完成: {expected_spc} 样本/通道")
                    self.lbl_status.setStyleSheet(
                        "color: green; font-weight: bold;")
                    self.log_signal.emit(
                        f"[INFO] Burst完成: {expected_spc} spc/ch")

        elif frame.cmd == CMD_ACK:
            pass

    # ----------------------------------------------------------------
    # Excel export
    # ----------------------------------------------------------------

    def _export_excel(self):
        log_dir = r"D:\test\STM32H723ZGT6\host\adc_logs"
        os.makedirs(log_dir, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = os.path.join(log_dir, f"adc_{ts}.xlsx")

        wb = openpyxl.Workbook()
        ws = wb.active
        ws.title = "ADC"
        ch_map = self._burst_ch_map
        active_pins = [CH_PINS[i] for i in ch_map]
        headers = ["样本序号"] + [f"{p} 电压(V)" for p in active_pins]
        for ci, h in enumerate(headers, 1):
            ws.cell(row=1, column=ci, value=h)

        spc = min(len(self._buffers[i]) if i in ch_map else 0
                   for i in ch_map)
        for r in range(spc):
            ws.cell(row=r + 2, column=1, value=r)
            for c, phys in enumerate(ch_map):
                raw = (self._buffers[phys][r]
                       if r < len(self._buffers[phys]) else 0)
                ws.cell(row=r + 2, column=c + 2,
                        value=round(raw * ADC_VREF / ADC_MAX - self._adc_offset, 4))
        wb.save(path)
        self.log_signal.emit(f"[INFO] Excel已保存: {path}")

    # ----------------------------------------------------------------
    # Plot update
    # ----------------------------------------------------------------

    def _update_plot(self):
        with self._lock:
            bufs = [list(b) for b in self._buffers]
        ch_map = self._burst_ch_map
        for i in range(NUM_CH):
            buf = bufs[i]
            if not buf or i not in ch_map:
                self._curves[i].setData([], [])
                continue
            arr = np.array(buf, dtype=np.float32)
            volts = arr * ADC_VREF / ADC_MAX - self._adc_offset  # ← corrected
            n = len(volts)
            offset = (self._offset_spins[i].value()
                      if i < len(self._offset_spins) else 0.0)

            if n > 8000:
                bs = max(n // 4000, 2)
                trim = n - (n % bs)
                v = volts[:trim].reshape(-1, bs)
                v_max = v.max(axis=1)
                v_min = v.min(axis=1)
                x = np.arange(0, trim, bs, dtype=np.float64)
                x2 = np.empty(len(v_max) * 2, dtype=np.float64)
                y2 = np.empty(len(v_max) * 2, dtype=np.float32)
                x2[0::2] = x
                x2[1::2] = x + bs
                y2[0::2] = v_min + offset
                y2[1::2] = v_max + offset
                self._curves[i].setData(x2, y2)
            else:
                self._curves[i].setData(np.arange(n), volts + offset)
