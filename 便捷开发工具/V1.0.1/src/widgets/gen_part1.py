# encoding: utf-8
import os

TARGET = os.path.join(os.path.dirname(__file__), 'wave_panel.py')

# Build the file content
lines = []
lines.append('# -*- coding: utf-8 -*-')
lines.append('"""Waveform panel - optimized version.')
lines.append('Performance: debounced plot, batch table ops, reused CLK markers, all channels editable.')
lines.append('"""')
lines.append('')
lines.append('import numpy as np')
lines.append('import pyqtgraph as pg')
lines.append('from PyQt6.QtWidgets import (')
lines.append('    QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLabel,')
lines.append('    QSpinBox, QDoubleSpinBox, QTableWidget, QTableWidgetItem,')
lines.append('    QGroupBox, QMessageBox, QLineEdit, QComboBox, QCheckBox,')
lines.append('    QHeaderView, QAbstractItemView, QSplitter, QSizePolicy,')
lines.append(')')
lines.append('from PyQt6.QtCore import Qt, pyqtSignal, QTimer')
lines.append('from typing import List')
lines.append('from comm.protocol import (')
lines.append('    build_wave_config, build_wave_data, build_wave_ctrl,')
lines.append('    CMD_ACK,')
lines.append(')')
lines.append('from comm.serial_link import SerialLink')
lines.append('')
lines.append('')
lines.append('CH_NAMES = [\"XYNC\", \"SCLK\", \"SH_R\", \"SH_S\", \"RST\"]')
lines.append('CH_PINS = [\"PA0\", \"PA1\", \"PA2\", \"PA3\", \"PA5\"]')
lines.append('CH_BITS = [0, 1, 2, 3, 5]')
lines.append('CH_COLORS = [(255,0,0),(0,255,0),(0,0,255),(255,255,0),(255,0,255)]')
lines.append('NUM_CH = 5')
lines.append('')

with open(TARGET, 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))
print('Part 1 written successfully')
