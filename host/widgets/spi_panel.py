"""SPI 寄存器写入面板 — 命令序列模式.

表格每行 = 一条寄存器写入命令:
    [勾选] [写寄存器] [地址] [数据] [延时(ms)]
- 点"写寄存器"标签列(列1)发送该单条
- 开始发送 = 按行顺序逐条发送(按各自延时等待)
- 支持新增/删除/全选切换/导入导出 (JSON)
"""

import json
import os
import shutil
import sys
from pathlib import Path
from typing import List, Tuple

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLabel,
    QSpinBox, QComboBox, QTableWidget, QTableWidgetItem,
    QGroupBox, QMessageBox, QCheckBox, QHeaderView,
    QLineEdit, QFileDialog,
)
from PyQt6.QtCore import Qt, pyqtSignal, pyqtSlot, QTimer

from comm.protocol import build_spi_config, build_spi_reg_writes
from comm.serial_link import SerialLink


def _resolve_config_dir():
    """配置文件目录，兼容源码运行 / PyInstaller 打包两种场景。"""
    if getattr(sys, 'frozen', False):
        exe_dir = os.path.dirname(sys.executable)        # host/dist
        host_dir = os.path.dirname(exe_dir)              # host
    else:
        host_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cfg = os.path.join(host_dir, "config")
    os.makedirs(cfg, exist_ok=True)
    return cfg


_CONFIG_DIR = _resolve_config_dir()
_SEND_PATH = os.path.join(_CONFIG_DIR, "spi_send.json")

# 首次运行：把旧位置的文件迁移过来
_OLD = os.path.join(os.path.expanduser("~"), ".h723_spi_send.json")
if os.path.exists(_OLD) and not os.path.exists(_SEND_PATH):
    try:
        shutil.copy2(_OLD, _SEND_PATH)
    except Exception:
        pass

# 列号
COL_CHECK  = 0
COL_LABEL  = 1
COL_ADDR   = 2
COL_DATA   = 3
COL_DELAY  = 4
COL_DEL    = 5


class SpiPanel(QWidget):
    log_signal = pyqtSignal(str)

    def __init__(self, link: SerialLink, parent=None):
        super().__init__(parent)
        self.link = link
        self._commands: list[dict] = []
        self._setup_ui()
        self._load()

    # ----------------------------------------------------------------
    # UI
    # ----------------------------------------------------------------
    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # --- SPI 配置 ---
        cfg_box = QGroupBox("SPI 配置")
        cfg_layout = QHBoxLayout(cfg_box)
        cfg_layout.addWidget(QLabel("CPOL:"))
        self.cb_cpol = QComboBox()
        self.cb_cpol.addItems(["0", "1"])
        cfg_layout.addWidget(self.cb_cpol)

        cfg_layout.addWidget(QLabel("CPHA:"))
        self.cb_cpha = QComboBox()
        self.cb_cpha.addItems(["0", "1"])
        cfg_layout.addWidget(self.cb_cpha)

        cfg_layout.addWidget(QLabel("分频器:"))
        self.cb_prescaler = QComboBox()
        for i, label in enumerate(["/2","/4","/8","/16","/32","/64","/128","256"]):
            self.cb_prescaler.addItem(label, i)
        self.cb_prescaler.setCurrentIndex(5)
        cfg_layout.addWidget(self.cb_prescaler)

        cfg_layout.addWidget(QLabel("位宽:"))
        self.cb_dwidth = QComboBox()
        self.cb_dwidth.addItem("16-bit", 16)
        cfg_layout.addWidget(self.cb_dwidth)

        self.btn_cfg = QPushButton("应用配置")
        self.btn_cfg.clicked.connect(self._send_config)
        cfg_layout.addWidget(self.btn_cfg)
        layout.addWidget(cfg_box)

        # --- 引脚映射 ---
        pin_box = QGroupBox("引脚映射")
        pin_layout = QHBoxLayout(pin_box)
        pin_layout.setSpacing(20)
        for pin, func in [("PB12","CS"),("PB13","CLK"),("PB14","MISO"),("PB15","MOSI")]:
            lbl = QLabel(f"<b>{pin}</b><br>{func}")
            lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            pin_layout.addWidget(lbl)
        layout.addWidget(pin_box)

        # --- 命令序列表 ---
        reg_box = QGroupBox("寄存器写入  (勾选后按顺序发送，每条按各自延时等待)")
        reg_layout = QVBoxLayout(reg_box)

        self.table = QTableWidget()
        self.table.setColumnCount(6)
        self.table.setHorizontalHeaderLabels(["勾选", "操作", "地址", "数据", "延时(ms)", "删除"])
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        self.table.setColumnWidth(0, 45)
        self.table.setColumnWidth(1, 70)
        self.table.setColumnWidth(4, 80)
        self.table.setColumnWidth(5, 50)
        self.table.setAlternatingRowColors(True)
        self.table.cellClicked.connect(self._on_cell_clicked)
        reg_layout.addWidget(self.table)

        # 工具栏
        tools = QHBoxLayout()
        self.btn_add = QPushButton("新增")
        self.btn_add.clicked.connect(self._add_row)
        tools.addWidget(self.btn_add)
        self.btn_del = QPushButton("删除")
        self.btn_del.clicked.connect(self._del_rows)
        tools.addWidget(self.btn_del)
        self.btn_toggle = QPushButton("全选/全不选")
        self.btn_toggle.clicked.connect(self._toggle_select)
        tools.addWidget(self.btn_toggle)
        self.btn_import = QPushButton("导入")
        self.btn_import.clicked.connect(self._import_json)
        tools.addWidget(self.btn_import)
        self.btn_export = QPushButton("导出")
        self.btn_export.clicked.connect(self._export_json)
        tools.addWidget(self.btn_export)
        tools.addStretch()
        reg_layout.addLayout(tools)

        # 发送控制
        send_layout = QHBoxLayout()
        self.btn_send = QPushButton("开始发送")
        self.btn_send.setStyleSheet("background-color:#2E7D32; color:white; font-weight:bold;")
        self.btn_send.clicked.connect(self._start_seq)
        send_layout.addWidget(self.btn_send)

        self.btn_stop = QPushButton("停止")
        self.btn_stop.setStyleSheet("background-color:#C62828; color:white;")
        self.btn_stop.clicked.connect(self._stop_seq)
        send_layout.addWidget(self.btn_stop)

        self.chk_burst = QCheckBox("持续发送")
        self.chk_burst.stateChanged.connect(self._toggle_burst)
        send_layout.addWidget(self.chk_burst)

        self.chk_nonstop = QCheckBox("不间断")
        self.chk_nonstop.stateChanged.connect(self._toggle_nonstop)
        send_layout.addWidget(self.chk_nonstop)

        send_layout.addWidget(QLabel("间隔(ms):"))
        self.sb_interval = QSpinBox()
        self.sb_interval.setRange(1, 1000)
        self.sb_interval.setValue(5)
        send_layout.addWidget(self.sb_interval)
        reg_layout.addLayout(send_layout)

        layout.addWidget(reg_box)
        layout.addStretch()

        self._burst_timer = QTimer(self)
        self._burst_timer.timeout.connect(self._start_seq)
        self._seq_running = False
        self._seq_index = 0

    # ----------------------------------------------------------------
    # 持久化
    # ----------------------------------------------------------------
    def _load(self):
        if os.path.exists(_SEND_PATH):
            try:
                with open(_SEND_PATH, "r", encoding="utf-8") as f:
                    raw = json.load(f)
                # 兼容旧格式 {"rows":[...]}
                rows = raw.get("rows", raw) if isinstance(raw, dict) else raw
                if isinstance(rows, list):
                    for r in rows:
                        self._commands.append({
                            "checked": bool(r.get("checked", True)),
                            "label": str(r.get("label", "写寄存器")),
                            "addr": str(r.get("addr", "00")),
                            "data": str(r.get("data", "00")),
                            "delay_ms": int(r.get("delay_ms", 100)),
                        })
            except Exception:
                self._commands = []
        if not self._commands:
            self._commands = [
                {"checked": True, "label": "写寄存器", "addr": "0x0E", "data": "0x0000", "delay_ms": 100},
                {"checked": True, "label": "写寄存器", "addr": "0x0A", "data": "0x7804", "delay_ms": 100},
            ]
            self._save()
        self._rebuild_table()

    def _save(self):
        try:
            with open(_SEND_PATH, "w", encoding="utf-8") as f:
                json.dump({"rows": self._commands}, f, ensure_ascii=False, indent=2)
        except Exception:
            pass

    def _rebuild_table(self):
        self.table.setRowCount(len(self._commands))
        for i, cmd in enumerate(self._commands):
            # 列0 勾选
            cb = QCheckBox()
            cb.setChecked(cmd.get("checked", True))
            cb_w = QWidget()
            l = QHBoxLayout(cb_w)
            l.addWidget(cb)
            l.setAlignment(Qt.AlignmentFlag.AlignCenter)
            l.setContentsMargins(0, 0, 0, 0)
            self.table.setCellWidget(i, COL_CHECK, cb_w)
            # 列1 操作标签
            self.table.setItem(i, COL_LABEL, QTableWidgetItem(cmd.get("label", "写寄存器")))
            # 列2 地址 (QLineEdit 防投影)
            addr_edit = QLineEdit(cmd.get("addr", "00"))
            addr_edit.setPlaceholderText("HEX 如: 0x0E")
            addr_edit.textChanged.connect(lambda t, idx=i: self._on_field_changed(idx, "addr", t))
            addr_edit.returnPressed.connect(lambda: self.table.setFocus())
            self.table.setCellWidget(i, COL_ADDR, addr_edit)
            # 列3 数据 (QLineEdit 防投影)
            data_edit = QLineEdit(cmd.get("data", "00"))
            data_edit.setPlaceholderText("HEX 如: 0x7804")
            data_edit.textChanged.connect(lambda t, idx=i: self._on_field_changed(idx, "data", t))
            data_edit.returnPressed.connect(lambda: self.table.setFocus())
            self.table.setCellWidget(i, COL_DATA, data_edit)
            # 列4 延时
            spin = QSpinBox()
            spin.setRange(0, 10000)
            spin.setSingleStep(10)
            spin.setValue(cmd.get("delay_ms", 100))
            spin.valueChanged.connect(lambda v, idx=i: self._on_field_changed(idx, "delay_ms", v))
            self.table.setCellWidget(i, COL_DELAY, spin)
            # 列5 单条删除
            del_btn = QPushButton("删除")
            del_btn.setStyleSheet("color:#C62828;")
            del_btn.clicked.connect(lambda _, idx=i: self._del_single(idx))
            self.table.setCellWidget(i, COL_DEL, del_btn)

    def _on_field_changed(self, idx: int, field, value):
        if idx < len(self._commands):
            self._commands[idx][field] = value

    def _sync_from_table(self):
        """把表格当前内容回写到 _commands"""
        while len(self._commands) < self.table.rowCount():
            self._commands.append({"checked": True, "label": "写寄存器", "addr": "00", "data": "00", "delay_ms": 100})
        for i in range(self.table.rowCount()):
            if i >= len(self._commands):
                break
            cb_w = self.table.cellWidget(i, COL_CHECK)
            cb = cb_w.findChild(QCheckBox) if cb_w else None
            addr_edit = self.table.cellWidget(i, COL_ADDR)
            data_edit = self.table.cellWidget(i, COL_DATA)
            spin = self.table.cellWidget(i, COL_DELAY)
            self._commands[i]["checked"] = cb.isChecked() if cb else True
            self._commands[i]["label"] = self.table.item(i, COL_LABEL).text() if self.table.item(i, COL_LABEL) else "写寄存器"
            self._commands[i]["addr"] = addr_edit.text() if addr_edit else "00"
            self._commands[i]["data"] = data_edit.text() if data_edit else "00"
            self._commands[i]["delay_ms"] = spin.value() if spin else 100
        self._save()

    # ----------------------------------------------------------------
    # 工具栏
    # ----------------------------------------------------------------
    def _add_row(self):
        row = self.table.rowCount()
        self.table.insertRow(row)
        cb = QCheckBox(); cb.setChecked(True)
        cb_w = QWidget(); l = QHBoxLayout(cb_w); l.addWidget(cb); l.setAlignment(Qt.AlignmentFlag.AlignCenter); l.setContentsMargins(0,0,0,0)
        self.table.setCellWidget(row, COL_CHECK, cb_w)
        self.table.setItem(row, COL_LABEL, QTableWidgetItem("写寄存器"))
        ae = QLineEdit("00"); ae.setPlaceholderText("HEX 如: 0x0E")
        ae.textChanged.connect(lambda t, idx=row: self._on_field_changed(idx, "addr", t))
        ae.returnPressed.connect(lambda: self.table.setFocus())
        self.table.setCellWidget(row, COL_ADDR, ae)
        de = QLineEdit("00"); de.setPlaceholderText("HEX 如: 0x7804")
        de.textChanged.connect(lambda t, idx=row: self._on_field_changed(idx, "data", t))
        de.returnPressed.connect(lambda: self.table.setFocus())
        self.table.setCellWidget(row, COL_DATA, de)
        sp = QSpinBox(); sp.setRange(0, 10000); sp.setValue(100)
        sp.valueChanged.connect(lambda v, idx=row: self._on_field_changed(idx, "delay_ms", v))
        self.table.setCellWidget(row, COL_DELAY, sp)
        del_btn = QPushButton("删除"); del_btn.setStyleSheet("color:#C62828;")
        del_btn.clicked.connect(lambda _, idx=row: self._del_single(idx))
        self.table.setCellWidget(row, COL_DEL, del_btn)
        self._commands.append({"checked": True, "label": "写寄存器", "addr": "00", "data": "00", "delay_ms": 100})
        self._save()

    def _del_single(self, idx: int):
        """单条删除：点该行删除按钮"""
        if idx < 0 or idx >= self.table.rowCount():
            return
        self.table.removeRow(idx)
        if idx < len(self._commands):
            self._commands.pop(idx)
        self._save()

    def _del_rows(self):
        """选中删除：删除表格当前选中的行"""
        selected = sorted(set(idx.row() for idx in self.table.selectedIndexes()), reverse=True)
        if not selected:
            self.log_signal.emit("[INFO] 没有选中任何行")
            return
        for r in selected:
            self.table.removeRow(r)
            if r < len(self._commands):
                self._commands.pop(r)
        self._save()

    def _toggle_select(self):
        all_checked = True
        for i in range(self.table.rowCount()):
            cb_w = self.table.cellWidget(i, COL_CHECK)
            cb = cb_w.findChild(QCheckBox) if cb_w else None
            if cb and not cb.isChecked():
                all_checked = False
                break
        new_val = not all_checked
        for i in range(self.table.rowCount()):
            cb_w = self.table.cellWidget(i, COL_CHECK)
            cb = cb_w.findChild(QCheckBox) if cb_w else None
            if cb:
                cb.setChecked(new_val)

    def _import_json(self):
        path, _ = QFileDialog.getOpenFileName(self, "导入 SPI 命令", "", "JSON (*.json)")
        if not path:
            return
        try:
            raw = json.loads(Path(path).read_text(encoding="utf-8"))
            rows = raw.get("rows", raw) if isinstance(raw, dict) else raw
            if not isinstance(rows, list):
                raise ValueError("格式错误")
            self._commands = []
            for r in rows:
                self._commands.append({
                    "checked": bool(r.get("checked", True)),
                    "label": str(r.get("label", "写寄存器")),
                    "addr": str(r.get("addr", "00")),
                    "data": str(r.get("data", "00")),
                    "delay_ms": int(r.get("delay_ms", 100)),
                })
            self._save()
            self._rebuild_table()
            self.log_signal.emit(f"[INFO] 已导入 {len(self._commands)} 条命令")
        except Exception as e:
            QMessageBox.warning(self, "导入失败", str(e))

    def _export_json(self):
        self._sync_from_table()
        path, _ = QFileDialog.getSaveFileName(self, "导出 SPI 命令", "spi_commands.json", "JSON (*.json)")
        if path:
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"rows": self._commands}, f, ensure_ascii=False, indent=2)
            self.log_signal.emit(f"[INFO] 已导出: {path}")

    # ----------------------------------------------------------------
    # 发送
    # ----------------------------------------------------------------
    def _on_cell_clicked(self, row: int, col: int):
        """点"写寄存器"标签列(列1)才发送该单条"""
        if col != COL_LABEL:
            return
        self._send_row(row)

    def _send_row(self, row: int):
        """发送指定行（列1 点击 或 地址/数据单元格回车触发）"""
        if self._seq_running:
            return
        if not self.link.is_open():
            self.log_signal.emit("[WARN] 串口未打开")
            return
        if row >= len(self._commands):
            return
        cmd = self._commands[row]
        ok, frame = self._build_single(cmd)
        if not ok:
            self.log_signal.emit(f"[WARN] 第{row+1}行地址/数据非法，跳过")
            return
        self.link.send(frame.to_bytes())
        self.log_signal.emit(f"[TX] 寄存器写入 @{cmd.get('addr','')} = {cmd.get('data','')}")

    def _build_single(self, cmd: dict):
        """单条命令 → Frame, 返回 (ok, frame)"""
        try:
            addr = int(cmd.get("addr", "00"), 16) & 0xFF
            data = int(cmd.get("data", "00"), 16)
        except ValueError:
            return False, None
        dwidth = self.cb_dwidth.currentData()
        frame = build_spi_reg_writes([(addr, data)], data_bits=dwidth)
        return True, frame

    def _start_seq(self):
        if not self.link.is_open():
            self.log_signal.emit("[WARN] 串口未打开")
            return
        self._sync_from_table()
        # 预校验
        bad = []
        for i, cmd in enumerate(self._commands):
            if not cmd.get("checked", True):
                continue
            try:
                int(cmd.get("addr", "00"), 16)
                int(cmd.get("data", "00"), 16)
            except ValueError:
                bad.append(i + 1)
        if bad:
            self.log_signal.emit(f"[ERROR] 第 {bad} 行地址/数据非法，已中止发送")
            return
        self._seq_running = True
        self._seq_index = 0
        self._seq_send_next()

    def _stop_seq(self):
        self._seq_running = False
        self.log_signal.emit("[INFO] 命令序列已停止")

    def _seq_send_next(self):
        if not self._seq_running:
            return
        # 找下一条已勾选的
        while self._seq_index < len(self._commands):
            cmd = self._commands[self._seq_index]
            if cmd.get("checked", True) and cmd.get("addr", "").strip() and cmd.get("data", "").strip():
                break
            self._seq_index += 1
        else:
            self._seq_running = False
            self.log_signal.emit("[INFO] 命令序列发送完毕")
            return
        ok, frame = self._build_single(cmd)
        if ok:
            self.link.send(frame.to_bytes())
            self.log_signal.emit(f"[TX] [{self._seq_index+1}] 寄存器 @{cmd.get('addr','')} = {cmd.get('data','')}")
        self._seq_index += 1
        delay = cmd.get("delay_ms", 100)
        QTimer.singleShot(delay, self._seq_send_next)

    def _toggle_burst(self, state):
        if state == Qt.CheckState.Checked.value:
            iv = 1 if self.chk_nonstop.isChecked() else self.sb_interval.value()
            self._burst_timer.start(iv)
            self.log_signal.emit(f"[INFO] 持续发送启动({iv}ms)")
        else:
            self._burst_timer.stop()
            self.log_signal.emit("[INFO] 持续发送已停止")

    def _toggle_nonstop(self, state):
        if state == Qt.CheckState.Checked.value:
            self.sb_interval.setEnabled(False)
            self.sb_interval.setValue(1)
            if self.chk_burst.isChecked():
                self._burst_timer.stop()
                self._burst_timer.start(1)
                self.log_signal.emit("[INFO] 不间断发送(1ms)")
        else:
            self.sb_interval.setEnabled(True)
            if self.chk_burst.isChecked():
                self._burst_timer.stop()
                self._burst_timer.start(self.sb_interval.value())

    # ----------------------------------------------------------------
    # SPI 配置 / 响应
    # ----------------------------------------------------------------
    def _send_config(self):
        if not self.link.is_open():
            self.log_signal.emit("[WARN] 串口未打开")
            return
        cpol = int(self.cb_cpol.currentText())
        cpha = int(self.cb_cpha.currentText())
        prescaler = self.cb_prescaler.currentData()
        dwidth = self.cb_dwidth.currentData()
        frame = build_spi_config(cpol, cpha, prescaler, dwidth)
        self.link.send(frame.to_bytes())
        self.log_signal.emit(
            f"[TX] SPI配置 CPOL={cpol} CPHA={cpha} "
            f"分频={self.cb_prescaler.currentText()} {dwidth}-bit"
        )

    def on_frame(self, frame):
        from comm.protocol import CMD_ACK
        if frame.cmd == CMD_ACK:
            if len(frame.payload) < 2:
                return
            ok = "成功" if frame.payload[1] == 0 else "失败"
            self.log_signal.emit(f"[RX] 确认 cmd=0x{frame.payload[0]:02X} {ok}")