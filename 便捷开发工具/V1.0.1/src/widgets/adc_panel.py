# -*- coding: utf-8 -*-
"""ADC 2-channel real-time acquisition panel (PA6 + optional PA7).

Fixed: rolling X-axis, reduced log spam, auto Y-range, improved rate limiting,
and guards against firmware missing ADC support to prevent MCU stalling.
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

from comm.protocol import build_adc_config, build_adc_ctrl, build_adc_burst, parse_adc_data, CMD_ACK, CMD_ADC_DATA, CMD_ADC_BURST
from comm.serial_link import SerialLink

CH_COLORS = [
    (255, 0, 0),     (0, 255, 0),
]

CH_PINS = ["PA6", "PA7"]
ADC_VREF = 3.3
ADC_MAX = 4095.0
WINDOW_SIZE = 50000
DISPLAY_MAX = 8000   # downsample threshold (fast rendering, 2000 buckets × 4 pts)
LOG_INTERVAL = 20
ADC_DATA_LOG_INTERVAL = 1   # log every ADC frame to CSV


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
        self._buffers = [deque(maxlen=WINDOW_SIZE) for _ in range(2)]
        self._total_samples = 0
        self._sample_rate = 10000
        self._lock = threading.Lock()
        self._frame_count = 0
        self._cfg_acked = False
        self._burst_mode = False
        self._burst_num_samples = 0

        # Setup ADC data log file (MUST be before _setup_ui)
        if getattr(sys, 'frozen', False):
            base_dir = os.path.dirname(os.path.dirname(sys.executable))
        else:
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        log_dir = os.path.join(base_dir, "..", "日志", "adc_logs")
        os.makedirs(log_dir, exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._adc_log_path = os.path.join(log_dir, f"adc_raw_{timestamp}.csv")
        self._adc_log_file = open(self._adc_log_path, "w", encoding="utf-8")
        self._adc_log_file.write("frame_seq,total_samples,ch_mask,ch_idx,raw_hex,raw_values\n")
        self._adc_log_file.flush()
        self._log_internal_counter = 0

        self._setup_ui()

        self._plot_timer = QTimer(self)
        self._plot_timer.timeout.connect(self._update_plot)

    def _log_adc_data(self, seq_id: int, ch_mask: int, raw: bytes, samples_reshaped):
        """Write detailed ADC data to log file for debugging."""
        if ADC_DATA_LOG_INTERVAL == 0:
            return
        try:
            self._log_internal_counter += 1
            if self._log_internal_counter % ADC_DATA_LOG_INTERVAL != 0:
                return

            # Raw hex dump (first 64 bytes)
            raw_hex = raw[:64].hex()

            # Write per-channel data
            num_rows = samples_reshaped.shape[0]
            for r in range(min(num_rows, 20)):  # Log first 20 scan periods
                for c in range(samples_reshaped.shape[1]):
                    val = int(samples_reshaped[r, c])
                    self._adc_log_file.write(
                        f"{seq_id},{self._total_samples + r},0x{ch_mask:02X},{c},{raw_hex},{val}\n"
                    )
            self._adc_log_file.flush()
        except Exception as e:
            # Silently ignore log errors
            pass

    def _close_adc_log(self):
        if hasattr(self, '_adc_log_file') and self._adc_log_file:
            try:
                self._adc_log_file.close()
            except Exception:
                pass
            self._adc_log_file = None

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # --- Config group ---
        cfg_box = QGroupBox("ADC 配置")
        cfg_layout = QHBoxLayout(cfg_box)

        cfg_layout.addWidget(QLabel("采样率 (Hz):"))
        self.sb_rate = QSpinBox()
        self.sb_rate.setRange(1000, 150000)
        self.sb_rate.setValue(10000)
        self.sb_rate.setSingleStep(1000)
        cfg_layout.addWidget(self.sb_rate)

        cfg_layout.addWidget(QLabel("模式:"))
        self.cb_mode = QComboBox()
        self.cb_mode.addItem("RAW_STREAM (A)", 0)
        self.cb_mode.addItem("PACKED (B预留)", 1)
        self.cb_mode.addItem("BURST (D预留)", 2)
        self.cb_mode.setCurrentIndex(0)
        cfg_layout.addWidget(self.cb_mode)

        self.btn_cfg = QPushButton("应用配置")
        self.btn_cfg.clicked.connect(self._send_config)
        cfg_layout.addWidget(self.btn_cfg)

        layout.addWidget(cfg_box)

        # --- Channel enable + pin map ---
        ch_box = QGroupBox("通道使能")
        ch_layout = QHBoxLayout(ch_box)
        self._ch_checks = []
        for i, pin in enumerate(CH_PINS):
            chk = QCheckBox(f"CH{i} ({pin})")
            # ponytail: PA6 enabled by default; PA7 disabled (floating input
            # contaminates PA6 data when continuous-scan channel order drifts).
            chk.setChecked(i == 0)
            chk.stateChanged.connect(self._update_rate_limit)
            self._ch_checks.append(chk)
            ch_layout.addWidget(chk)
        layout.addWidget(ch_box)
        self._update_rate_limit()

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
        self.btn_start = QPushButton("开始")
        self.btn_start.clicked.connect(lambda: self._send_ctrl(True))
        ctrl_layout.addWidget(self.btn_start)

        self.btn_stop = QPushButton("停止")
        self.btn_stop.clicked.connect(lambda: self._send_ctrl(False))
        ctrl_layout.addWidget(self.btn_stop)

        self.btn_clear = QPushButton("清空波形")
        self.btn_clear.clicked.connect(self._clear_buffers)
        ctrl_layout.addWidget(self.btn_clear)

        # --- Burst capture controls ---
        ctrl_layout.addWidget(QLabel("快照样本数:"))
        self.sb_burst_samples = QSpinBox()
        self.sb_burst_samples.setRange(100, 50000)
        self.sb_burst_samples.setValue(1000)
        self.sb_burst_samples.setSingleStep(100)
        ctrl_layout.addWidget(self.sb_burst_samples)

        self.btn_burst = QPushButton("单次快照")
        self.btn_burst.clicked.connect(self._on_burst_clicked)
        ctrl_layout.addWidget(self.btn_burst)

        self.cb_autorange = QCheckBox("自动Y轴")
        self.cb_autorange.setChecked(True)
        ctrl_layout.addWidget(self.cb_autorange)

        self.cb_follow = QCheckBox("跟随最新数据")
        self.cb_follow.setChecked(True)
        self.cb_follow.toggled.connect(self._on_follow_toggled)
        ctrl_layout.addWidget(self.cb_follow)

        # X-axis mode: sample index or time
        self.rb_sample = QRadioButton("样本序号")
        self.rb_time = QRadioButton("时间(s)")
        self.rb_sample.setChecked(True)
        self._x_mode_group = QButtonGroup(self)
        self._x_mode_group.addButton(self.rb_sample, 0)
        self._x_mode_group.addButton(self.rb_time, 1)
        ctrl_layout.addWidget(self.rb_sample)
        ctrl_layout.addWidget(self.rb_time)

        # Connect ViewBox user interaction → disable follow
        self.plot_widget.getViewBox().user_interacted.connect(self._on_user_interact)

        # Status label
        self.lbl_adc_status = QLabel("ADC 未配置")
        self.lbl_adc_status.setStyleSheet("color: gray;")
        ctrl_layout.addWidget(self.lbl_adc_status)

        # Log file indicator
        self.lbl_log_path = QLabel("")
        self.lbl_log_path.setStyleSheet("color: gray; font-size: 9pt;")
        ctrl_layout.addWidget(self.lbl_log_path)

        layout.addWidget(ctrl_box)
        layout.addStretch()

        # Show log file name
        self.lbl_log_path.setText(f"日志: {os.path.basename(self._adc_log_path)}")
        self.log_signal.emit(f"[INFO] ADC原始数据日志: {self._adc_log_path}")

    @staticmethod
    def _max_sample_rate(num_enabled: int) -> int:
        if num_enabled <= 0:
            return 0
        # Firmware ADC bandwidth cap: 400k samples/s total
        fw_rate = 400000 // num_enabled
        # USB FS bulk throughput cap: ~32k 16-bit samples/s total
        usb_rate = 32000 // num_enabled
        return min(fw_rate, usb_rate, 150000)

    def _update_rate_limit(self):
        num = bin(self._ch_mask()).count("1")
        max_rate = self._max_sample_rate(num)
        self.sb_rate.setMaximum(max_rate)
        if self.sb_rate.value() > max_rate:
            self.sb_rate.setValue(max_rate)

    def _on_burst_clicked(self):
        """Send a burst capture request, clear buffers, enter burst-wait mode."""
        ch_mask = self._ch_mask()
        if ch_mask == 0:
            QMessageBox.warning(self, "Burst", "请先选择至少一个通道")
            return

        num_samples = self.sb_burst_samples.value()
        # Clamp to WINDOW_SIZE so rolling buffer doesn't overflow
        if num_samples > WINDOW_SIZE:
            num_samples = WINDOW_SIZE
            self.sb_burst_samples.setValue(WINDOW_SIZE)

        with self._lock:
            for b in self._buffers:
                b.clear()
            self._total_samples = 0

        self._burst_mode = True
        self._burst_num_samples = num_samples
        self.lbl_adc_status.setText(f"Burst等待中 (目标 {num_samples} 样本)...")
        self.lbl_adc_status.setStyleSheet("color: orange; font-weight: bold;")

        frame = build_adc_burst(ch_mask, num_samples)
        self.link.send(frame)
        self.log_signal.emit(f"[TX] Burst请求 ch_mask=0x{ch_mask:02X} n={num_samples}")

    def _ch_mask(self) -> int:
        mask = 0
        for i, chk in enumerate(self._ch_checks):
            if chk.isChecked():
                mask |= (1 << i)
        return mask

    def _send_config(self):
        if not self.link.is_open():
            QMessageBox.warning(self, "未连接", "串口未打开。")
            return
        mask = self._ch_mask()
        if mask == 0:
            QMessageBox.warning(self, "通道为空", "至少选择一个通道。")
            return
        rate = self.sb_rate.value()
        mode = self.cb_mode.currentData()
        self._sample_rate = rate
        self._cfg_acked = False
        self._burst_mode = False
        self._burst_num_samples = 0
        frame = build_adc_config(mask, rate, mode)
        self.link.send(frame.to_bytes())
        self.log_signal.emit(f"[TX] ADC配置 通道=0x{mask:02X} 采样率={rate}Hz 模式={mode}")
        self.lbl_adc_status.setText("等待配置确认...")
        self.lbl_adc_status.setStyleSheet("color: orange;")

    def _send_ctrl(self, start: bool):
        if not self.link.is_open():
            QMessageBox.warning(self, "未连接", "串口未打开。")
            return

        if start and not self._cfg_acked:
            reply = QMessageBox.warning(
                self,
                "ADC 未就绪",
                ("固件可能不支持 ADC 功能，或尚未收到配置确认。\n\n"
                 "是否仍然发送开始命令？（可能导致单片机卡死）\n\n"
                 "建议：先点击应用配置并确认收到成功回复。"),
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No,
            )
            if reply != QMessageBox.StandardButton.Yes:
                return

        frame = build_adc_ctrl(start)
        self.link.send(frame.to_bytes())
        self.log_signal.emit(f"[TX] ADC控制 {'开始' if start else '停止'}")
        if start:
            self._clear_buffers()
            self._total_samples = 0
            self._frame_count = 0
            self._log_internal_counter = 0
            self.cb_follow.setChecked(True)
            self._plot_timer.start(80)
        else:
            self._plot_timer.stop()

    def _on_user_interact(self):
        """Called when user manually pans/zooms the plot — disable follow mode."""
        self.cb_follow.setChecked(False)

    def _on_follow_toggled(self, checked):
        """When follow is re-enabled, auto-range X to latest data."""
        if checked and self._total_samples > 0:
            divisor = self._sample_rate if self.rb_time.isChecked() and self._sample_rate > 0 else 1
            x_max = self._total_samples / divisor
            span = x_max * 0.1 + 1.0
            self.plot_widget.setXRange(max(0, x_max - span), x_max, padding=0.0)

    def _clear_buffers(self):
        with self._lock:
            for buf in self._buffers:
                buf.clear()
            self._total_samples = 0
        for curve in self._curves:
            curve.setData([], [])

    def _update_plot(self):
        with self._lock:
            total = self._total_samples
            buffers_snapshot = [list(buf) for buf in self._buffers]

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
                self.plot_widget.setYRange(max(0, vmin - margin), min(ADC_VREF, vmax + margin), padding=0.0)

        for i in range(2):
            buf = buffers_snapshot[i]
            if not buf:
                self._curves[i].setData([], [])
                continue
            arr = np.array(buf, dtype=np.float32)
            volts = arr * ADC_VREF / ADC_MAX
            start_idx = (total - len(buf)) / divisor
            step = 1.0 / divisor if use_time else 1.0
            x = np.arange(start_idx, start_idx + len(volts) * step, step, dtype=np.float64)
            # Down-sample for rendering.
            # ponytail: the old min-max interleave (v_min, v_max at the same x_mid)
            # turned every bucket into a vertical line — square waves looked like
            # "stacked red vertical bars" instead of flat-top pulses.
            #
            # New strategy: build step-like envelopes so that high/low plateaus
            # are drawn as horizontal segments.
            #   Per bucket: [x_left,v_min] → [x_left,v_max] → [x_right,v_max] → [x_right,v_min]
            # This draws a filled rectangle from min to max across the bucket width.
            if len(volts) > DISPLAY_MAX:
                n_buckets = DISPLAY_MAX // 2
                bucket_size = max(len(volts) // n_buckets, 2)
                trimmed = len(volts) - (len(volts) % bucket_size)
                if trimmed > 0:
                    volts_2d = volts[:trimmed].reshape(-1, bucket_size)
                    x_2d = x[:trimmed].reshape(-1, bucket_size)
                    v_min = volts_2d.min(axis=1)
                    v_max = volts_2d.max(axis=1)
                    x_left = x_2d[:, 0]
                    x_right = x_2d[:, -1]

                    # 4 points per bucket: left/min, left/max, right/max, right/min
                    volts_out = np.empty(n_buckets * 4, dtype=np.float32)
                    x_out = np.empty(n_buckets * 4, dtype=np.float64)
                    volts_out[0::4] = v_min
                    volts_out[1::4] = v_max
                    volts_out[2::4] = v_max
                    volts_out[3::4] = v_min
                    x_out[0::4] = x_left
                    x_out[1::4] = x_left
                    x_out[2::4] = x_right
                    x_out[3::4] = x_right
                    volts = volts_out
                    x = x_out
            self._curves[i].setData(x, volts)

        # Auto-scroll X if following latest data
        if self.cb_follow.isChecked() and total > 0:
            x_max = total / divisor
            span = x_max * 0.1 + 1.0
            x_min = max(0, x_max - span)
            self.plot_widget.setXRange(x_min, x_max, padding=0.0)

    def on_frame(self, frame):
        if frame.cmd == CMD_ADC_DATA:
            try:
                seq_id, ch_mask, mode, raw = parse_adc_data(frame.payload)
            except ValueError as e:
                self.log_signal.emit(f"[RX] ADC数据解析错误: {e}")
                return

            self._frame_count += 1
            if self._frame_count % LOG_INTERVAL == 1:
                self.log_signal.emit(
                    f"[RX] ADC数据 seq={seq_id} ch_mask=0x{ch_mask:02X} raw_len={len(raw)}")

            samples = np.frombuffer(raw, dtype=np.uint16)
            if len(samples) == 0:
                return

            num_enabled = bin(ch_mask).count("1")
            if num_enabled == 0:
                return

            scan_periods = len(samples) // num_enabled
            if scan_periods == 0:
                return
            samples = samples[:scan_periods * num_enabled]
            reshaped = samples.reshape(scan_periods, num_enabled)

            # Log raw data for debugging
            self._log_adc_data(seq_id, ch_mask, raw, reshaped)

            # Batch append: build channel slices outside the lock, then extend
            # buffers under the lock with minimal hold time.
            new_slices = []
            ch_idx = 0
            for i in range(2):
                if ch_mask & (1 << i):
                    new_slices.append((i, reshaped[:, ch_idx]))
                    ch_idx += 1

            with self._lock:
                if self._burst_mode:
                    # Burst mode: replace buffer with this batch
                    self._burst_mode = False
                    for i, col in new_slices:
                        self._buffers[i].clear()
                        self._buffers[i].extend(col)
                    self._total_samples = scan_periods
                    self._sample_rate = 0  # X-axis = sample index, not time
                    self.lbl_adc_status.setText(f"Burst完成: {self._total_samples} 样本")
                    self.lbl_adc_status.setStyleSheet("color: green; font-weight: bold;")
                    # Log all burst raw values to CSV
                    try:
                        for r in range(reshaped.shape[0]):
                            for c in range(reshaped.shape[1]):
                                val = int(reshaped[r, c])
                                self._adc_log_file.write(
                                    f"{seq_id},{r},0x{ch_mask:02X},{c},{raw[:64].hex()},{val}\n")
                        self._adc_log_file.flush()
                    except Exception:
                        pass
                else:
                    # Stream mode: append to rolling buffer
                    for i, col in new_slices:
                        self._buffers[i].extend(col)
                    self._total_samples += scan_periods

        elif frame.cmd == CMD_ACK:
            if len(frame.payload) < 2:
                return
            cmd_id = frame.payload[0]
            ok = frame.payload[1] == 0
            ok_str = "成功" if ok else "失败"
            cmd_name = {0x10: "ADC_CONFIG", 0x11: "ADC_CTRL"}.get(cmd_id, f"0x{cmd_id:02X}")

            if cmd_id == 0x10:
                self._cfg_acked = ok
                if ok:
                    self.lbl_adc_status.setText("ADC 已就绪")
                    self.lbl_adc_status.setStyleSheet("color: green; font-weight: bold;")
                else:
                    self.lbl_adc_status.setText("配置被固件拒绝（可能不支持ADC）")
                    self.lbl_adc_status.setStyleSheet("color: red;")

            self.log_signal.emit(f"[RX] 确认 {cmd_name} {ok_str}")
