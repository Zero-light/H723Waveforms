# -*- coding: utf-8 -*-
"""ADC single-shot burst capture panel (PA6 + optional PA7).

DMA-driven architecture (V2):
  Host sends CMD_ADC_CONFIG (ch_mask + sample_rate), then CMD_ADC_BURST.
  Firmware uses TIM3→ADC1→DMA hardware capture, then uploads the entire
  buffer as a sequence of CMD_ADC_DATA frames (1022 samples/frame max).
  Host accumulates all frames, then displays the complete waveform.

No streaming mode — every acquisition is a one-shot burst with
hardware-precise sample timing.
"""

import os
import sys
import threading
from collections import deque
from datetime import datetime

import numpy as np
import pyqtgraph as pg
from PyQt6.QtCore import Qt, QTimer, pyqtSignal
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLabel,
    QSpinBox, QComboBox, QGroupBox, QMessageBox, QCheckBox,
    QRadioButton, QButtonGroup,
)

from comm.protocol import build_adc_config, build_adc_ctrl, build_adc_burst, parse_adc_data
from comm.protocol import CMD_ACK, CMD_ADC_DATA
from comm.serial_link import SerialLink

CH_COLORS = [(255, 0, 0), (0, 255, 0)]
CH_PINS   = ["PA6", "PA7"]
ADC_VREF  = 3.3
ADC_MAX   = 4095.0

class AdcViewBox(pg.ViewBox):
    user_interacted = pyqtSignal()

    def mouseDragEvent(self, ev, axis=None):
        self.user_interacted.emit()
        super().mouseDragEvent(ev, axis=axis)

    def wheelEvent(self, ev, axis=None):
        self.user_interacted.emit()
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
        self._sample_rate = 20000  # default sample rate (Hz)
        self._lock = threading.Lock()

        # Burst state
        self._burst_pending = False   # waiting for firmware ACK
        self._burst_expect  = 0       # total samples expected
        self._burst_received = 0      # total samples received so far
        self._burst_num_ch   = 0      # number of enabled channels
        self._burst_ch_mask  = 0
        self._burst_buf       = []    # per-channel list accumulator

        # Display buffers
        self._buffers = [[] for _ in range(2)]

        # Setup ADC data log
        if getattr(sys, 'frozen', False):
            base_dir = os.path.dirname(os.path.dirname(sys.executable))
        else:
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        log_dir = os.path.join(base_dir, "..", "日志", "adc_logs")
        os.makedirs(log_dir, exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._adc_log_path = os.path.join(log_dir, f"adc_raw_{timestamp}.csv")
        self._adc_log_file = open(self._adc_log_path, "w", encoding="utf-8")
        self._adc_log_file.write("frame_offset,total_samples,ch_mask,ch_idx,raw_hex,raw_values\n")
        self._adc_log_file.flush()

        self._setup_ui()

        self._plot_timer = QTimer(self)
        self._plot_timer.timeout.connect(self._update_plot)
        self._plot_timer.start(100)  # 10 fps refresh

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # --- Config group ---
        cfg_box = QGroupBox("ADC 配置")
        cfg_layout = QHBoxLayout(cfg_box)

        cfg_layout.addWidget(QLabel("采样率 (Hz):"))
        self.sb_rate = QSpinBox()
        self.sb_rate.setRange(1000, 150000)
        self.sb_rate.setValue(20000)
        self.sb_rate.setSingleStep(1000)
        self.sb_rate.valueChanged.connect(self._on_rate_changed)
        cfg_layout.addWidget(self.sb_rate)

        self.btn_cfg = QPushButton("应用配置")
        self.btn_cfg.clicked.connect(self._send_config)
        cfg_layout.addWidget(self.btn_cfg)

        layout.addWidget(cfg_box)

        # --- Channel enable ---
        ch_box = QGroupBox("通道使能")
        ch_layout = QHBoxLayout(ch_box)
        self._ch_checks = []
        for i, pin in enumerate(CH_PINS):
            chk = QCheckBox(f"CH{i} ({pin})")
            chk.setChecked(i == 0)  # PA6 enabled by default
            self._ch_checks.append(chk)
            ch_layout.addWidget(chk)
        layout.addWidget(ch_box)

        # --- Plot ---
        plot_box = QGroupBox("实时波形（滚轮=纵向，Ctrl+滚轮=横向）")
        plot_layout = QVBoxLayout(plot_box)
        self.plot_widget = pg.PlotWidget(viewBox=AdcViewBox())
        self.plot_widget.setLabel("left", "电压", units="V")
        self.plot_widget.setLabel("bottom", "样本序号")
        self.plot_widget.setYRange(0, ADC_VREF, padding=0.05)
        self.plot_widget.showGrid(x=True, y=True)
        self.plot_widget.setAutoVisible(y=True)
        self._curves = []
        for i in range(2):
            pen = pg.mkPen(color=CH_COLORS[i], width=1.5)
            curve = self.plot_widget.plot(pen=pen, name=f"CH{i}")
            self._curves.append(curve)
        plot_layout.addWidget(self.plot_widget)
        layout.addWidget(plot_box)

        # --- Control ---
        ctrl_box = QGroupBox("采集控制")
        ctrl_layout = QHBoxLayout(ctrl_box)

        self.btn_burst = QPushButton("单次快照")
        self.btn_burst.clicked.connect(self._on_burst_clicked)
        self.btn_burst.setStyleSheet("font-weight: bold; min-height: 28px;")
        ctrl_layout.addWidget(self.btn_burst)

        self.btn_clear = QPushButton("清空波形")
        self.btn_clear.clicked.connect(self._clear_buffers)
        ctrl_layout.addWidget(self.btn_clear)

        ctrl_layout.addWidget(QLabel("快照样本数:"))
        self.sb_burst_samples = QSpinBox()
        self.sb_burst_samples.setRange(100, 32768)
        self.sb_burst_samples.setValue(32768)
        self.sb_burst_samples.setSingleStep(1000)
        ctrl_layout.addWidget(self.sb_burst_samples)

        self.cb_autorange = QCheckBox("自动Y轴")
        self.cb_autorange.setChecked(True)
        ctrl_layout.addWidget(self.cb_autorange)

        # X-axis mode
        self.rb_sample = QRadioButton("样本序号")
        self.rb_time = QRadioButton("时间(s)")
        self.rb_sample.setChecked(True)
        self._x_mode_group = QButtonGroup(self)
        self._x_mode_group.addButton(self.rb_sample, 0)
        self._x_mode_group.addButton(self.rb_time, 1)
        ctrl_layout.addWidget(self.rb_sample)
        ctrl_layout.addWidget(self.rb_time)

        self.plot_widget.getViewBox().user_interacted.connect(
            lambda: self._on_user_interact)

        # Status
        self.lbl_adc_status = QLabel("ADC: 待配置")
        self.lbl_adc_status.setStyleSheet("color: gray;")
        ctrl_layout.addWidget(self.lbl_adc_status)

        self.lbl_log_path = QLabel("")
        self.lbl_log_path.setStyleSheet("color: gray; font-size: 9pt;")
        ctrl_layout.addWidget(self.lbl_log_path)

        layout.addWidget(ctrl_box)
        layout.addStretch()

        self.lbl_log_path.setText(f"日志: {os.path.basename(self._adc_log_path)}")
        self.log_signal.emit(f"[INFO] ADC原始数据日志: {self._adc_log_path}")

    def _on_rate_changed(self):
        pass

    def _ch_mask(self) -> int:
        mask = 0
        for i, chk in enumerate(self._ch_checks):
            if chk.isChecked():
                mask |= (1 << i)
        return mask

    # ================================================================
    # Protocol
    # ================================================================

    def _send_config(self):
        if not self.link.is_open():
            QMessageBox.warning(self, "未连接", "串口未打开。")
            return
        mask = self._ch_mask()
        if mask == 0:
            QMessageBox.warning(self, "通道为空", "至少选择一个通道。")
            return
        rate = self.sb_rate.value()
        self._sample_rate = rate
        frame = build_adc_config(mask, rate, mode=0)
        self.link.send(frame.to_bytes())
        self.log_signal.emit(
            f"[TX] ADC配置 通道=0x{mask:02X} 采样率={rate}Hz")
        self.lbl_adc_status.setText("配置已发送，等待确认...")
        self.lbl_adc_status.setStyleSheet("color: orange;")

    def _on_burst_clicked(self):
        if not self.link.is_open():
            QMessageBox.warning(self, "未连接", "串口未打开。")
            return
        ch_mask = self._ch_mask()
        if ch_mask == 0:
            QMessageBox.warning(self, "Burst", "请先选择至少一个通道。")
            return

        num_samples = self.sb_burst_samples.value()
        num_ch = bin(ch_mask).count("1")

        self._burst_pending  = True
        self._burst_expect   = num_samples * num_ch  # total uint16 values
        self._burst_received = 0
        self._burst_num_ch   = num_ch
        self._burst_ch_mask  = ch_mask
        self._burst_buf      = [[] for _ in range(num_ch)]

        self.lbl_adc_status.setText(
            f"快照进行中... 期望 {num_samples} 样本")
        self.lbl_adc_status.setStyleSheet("color: orange; font-weight: bold;")

        frame = build_adc_burst(ch_mask, num_samples)
        self.link.send(frame.to_bytes())
        self.log_signal.emit(
            f"[TX] ADC_BURST ch=0x{ch_mask:02X} n={num_samples}")

    def _clear_buffers(self):
        with self._lock:
            for b in self._buffers:
                b.clear()
        for curve in self._curves:
            curve.setData([], [])

    def _on_user_interact(self):
        pass

    # ================================================================
    # Frame handler
    # ================================================================

    def on_frame(self, frame):
        if frame.cmd == CMD_ADC_DATA:
            try:
                frame_offset, ch_mask, mode, raw = parse_adc_data(frame.payload)
            except ValueError as e:
                self.log_signal.emit(f"[RX] ADC数据解析错误: {e}")
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

            self.log_signal.emit(
                f"[RX] ADC数据 offset={frame_offset} "
                f"samples={scan_periods} ch_mask=0x{ch_mask:02X}")

            # --- Burst accumulation ---
            if self._burst_pending and ch_mask == self._burst_ch_mask:
                buf = self._burst_buf
                for c in range(num_en):
                    buf[c].append(reshaped[:, c].copy())
                self._burst_received += scan_periods
                expected_samples_per_ch = self._burst_expect // num_en

                # Log burst data
                try:
                    for r in range(min(scan_periods, 5)):
                        for c in range(num_en):
                            val = int(reshaped[r, c])
                            self._adc_log_file.write(
                                f"{frame_offset},"
                                f"{self._burst_received - scan_periods + r},"
                                f"0x{ch_mask:02X},{c},"
                                f"{raw[:64].hex()},{val}\n")
                    self._adc_log_file.flush()
                except Exception:
                    pass

                if self._burst_received >= expected_samples_per_ch:
                    # All frames received — build display buffers
                    with self._lock:
                        for c in range(num_en):
                            if buf[c]:
                                ch_data = np.concatenate(buf[c])
                                self._buffers[c] = list(ch_data)
                            else:
                                self._buffers[c] = []
                    for c in range(num_en, 2):
                        self._buffers[c] = []

                    self._burst_pending = False
                    self.lbl_adc_status.setText(
                        f"快照完成: {expected_samples_per_ch} 样本/通道")
                    self.lbl_adc_status.setStyleSheet(
                        "color: green; font-weight: bold;")

                    self.log_signal.emit(
                        f"[INFO] Burst完成: {expected_samples_per_ch} 样本/通道")
                    self._update_plot()

        elif frame.cmd == CMD_ACK:
            if len(frame.payload) < 2:
                return
            cmd_id = frame.payload[0]
            ok = frame.payload[1] == 0
            ok_str = "成功" if ok else "失败"
            cmd_name = {
                0x10: "ADC_CONFIG",
                0x11: "ADC_CTRL",
                0x13: "ADC_BURST",
            }.get(cmd_id, f"0x{cmd_id:02X}")

            if cmd_id == 0x10 and ok:
                self.lbl_adc_status.setText("ADC 已就绪")
                self.lbl_adc_status.setStyleSheet(
                    "color: green; font-weight: bold;")

            if cmd_id == 0x13 and not ok:
                self._burst_pending = False
                self.lbl_adc_status.setText("快照被拒绝（固件忙或配置错误）")
                self.lbl_adc_status.setStyleSheet("color: red;")

            self.log_signal.emit(f"[RX] 确认 {cmd_name} {ok_str}")

    # ================================================================
    # Plot update
    # ================================================================

    def _update_plot(self):
        with self._lock:
            buffers_snapshot = [list(b) for b in self._buffers]

        use_time = self.rb_time.isChecked()
        divisor = self._sample_rate if use_time and self._sample_rate > 0 else 1

        if self.cb_autorange.isChecked():
            all_vals = []
            for buf in buffers_snapshot:
                if buf:
                    all_vals.extend(buf)
            if all_vals:
                arr_all = np.array(all_vals, dtype=np.float32) * ADC_VREF / ADC_MAX
                vmin = float(np.min(arr_all))
                vmax = float(np.max(arr_all))
                margin = (vmax - vmin) * 0.1 + 0.05
                self.plot_widget.setYRange(
                    max(0, vmin - margin),
                    min(ADC_VREF, vmax + margin),
                    padding=0.0)

        for i in range(2):
            buf = buffers_snapshot[i]
            if not buf:
                self._curves[i].setData([], [])
                continue
            arr = np.array(buf, dtype=np.float32)
            volts = arr * ADC_VREF / ADC_MAX
            step = 1.0 / divisor if use_time else 1.0
            x = np.arange(0, len(volts) * step, step, dtype=np.float64)

            # Down-sample for rendering: min-max envelope per bucket
            DISPLAY_MAX = 8000
            if len(volts) > DISPLAY_MAX:
                n_buckets = DISPLAY_MAX // 2
                bucket_size = max(len(volts) // n_buckets, 2)
                trimmed = len(volts) - (len(volts) % bucket_size)
                if trimmed > 0:
                    v2d = volts[:trimmed].reshape(-1, bucket_size)
                    x2d = x[:trimmed].reshape(-1, bucket_size)
                    v_min = v2d.min(axis=1)
                    v_max = v2d.max(axis=1)
                    x_left = x2d[:, 0]
                    x_right = x2d[:, -1]

                    n = n_buckets
                    vo = np.empty(n * 4, dtype=np.float32)
                    xo = np.empty(n * 4, dtype=np.float64)
                    vo[0::4] = v_min
                    vo[1::4] = v_max
                    vo[2::4] = v_max
                    vo[3::4] = v_min
                    xo[0::4] = x_left
                    xo[1::4] = x_left
                    xo[2::4] = x_right
                    xo[3::4] = x_right
                    volts = vo
                    x = xo

            self._curves[i].setData(x, volts)

    def _on_burst_ready(self, total_samples, sample_rate):
        """Called when all burst frames have been accumulated."""
        # _plot_timer not started — data is rendered immediately
        self._update_plot()
