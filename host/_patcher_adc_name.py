import sys, os
sys.stdout.reconfigure(encoding='utf-8')
path = 'D:/test/STM32H723ZGT6/host/widgets/adc_panel.py'
with open(path, 'r', encoding='utf-8') as f:
    c = f.read()

# 1: QLineEdit import
c = c.replace('    QSplitter, QFileDialog,', '    QSplitter, QFileDialog, QLineEdit,')
print('1 OK', flush=True)

# 2: name_edits/name_labels init
c = c.replace('        self._offset_spins = []', '        self._offset_spins = []\n        self._name_edits = []\n        self._name_labels = []', 1)
print('2 OK', flush=True)

# 3: Add name row before off_row = QHBoxLayout()
q = chr(34)
old3 = '\n        off_row = QHBoxLayout()'
new3 = (
    '\n'
    '        # ---- Name row ----\n'
    '        name_row = QHBoxLayout()\n'
    '        name_row.addWidget(QLabel(' + q + '\u6ce2\u5f62\u540d:' + q + '))\n'
    '        self._name_edits = []\n'
    '        for i in range(NUM_CH):\n'
    '            color = CH_COLORS[i]\n'
    '            lbl = QLabel(CH_NAMES[i])\n'
    '            lbl.setStyleSheet(' + q + 'color: rgb(' + q + ' + str(color[0]) + ' + q + ',' + q + ' + str(color[1]) + ' + q + ',' + q + ' + str(color[2]) + ' + q + '); font-weight: bold' + q + ')\n'
    '            name_row.addWidget(lbl)\n'
    '            edit = QLineEdit(CH_NAMES[i])\n'
    '            edit.setMaximumWidth(90)\n'
    '            edit.textChanged.connect(lambda text, ii=i: self._on_name_changed(ii, text))\n'
    '            name_row.addWidget(edit)\n'
    '            self._name_edits.append(edit)\n'
    '        name_row.addStretch()\n'
    '        outer.addLayout(name_row)\n'
    '\n'
    '        off_row = QHBoxLayout()'
)
assert old3 in c, 'm3'
c = c.replace(old3, new3, 1)
print('3 OK', flush=True)

# 4: TextItem
old4 = '            pen = pg.mkPen(color=CH_COLORS[i], width=1.5)\n            curve = pw.plot(pen=pen, name=CH_NAMES[i])\n\n            self._plots.append(pw)'
new4 = (
    '            pen = pg.mkPen(color=CH_COLORS[i], width=1.5)\n'
    '            curve = pw.plot(pen=pen, name=CH_NAMES[i])\n'
    '\n'
    '            txt = pg.TextItem(\n'
    '                text=CH_NAMES[i],\n'
    '                color=color,\n'
    '                anchor=(0, 0.5),\n'
    '                fill=pg.mkColor(0, 0, 0, 160),\n'
    '            )\n'
    '            txt.setZValue(10)\n'
    '            pw.addItem(txt)\n'
    '            self._name_labels.append(txt)\n'
    '\n'
    '            self._plots.append(pw)'
)
assert old4 in c, 'm4'
c = c.replace(old4, new4, 1)
print('4 OK', flush=True)

# 5: _on_name_changed
old5 = '    def _on_go(self):'
new5 = (
    '    def _on_name_changed(self, idx, text):\n'
    '        if idx < len(self._plots):\n'
    '            self._plots[idx].setLabel(' + q + 'left' + q + ', text, units=' + q + 'V' + q + ')\n'
    '        if idx < len(self._name_labels):\n'
    '            self._name_labels[idx].setText(text)\n'
    '\n'
    '    def _on_go(self):'
)
assert old5 in c, 'm5'
c = c.replace(old5, new5, 1)
print('5 OK', flush=True)

# 6: setPos
old6 = '            if n > 8000:'
new6 = (
    '            if i < len(self._name_labels):\n'
    '                first_val = volts[0] + offset if n > 0 else offset\n'
    '                self._name_labels[i].setPos(0, first_val)\n'
    '\n'
    '            if n > 8000:'
)
assert old6 in c, 'm6'
c = c.replace(old6, new6, 1)
print('6 OK', flush=True)

with open(path, 'w', encoding='utf-8') as f:
    f.write(c)
import py_compile
py_compile.compile(path, doraise=True)
print('Syntax OK', flush=True)

for k in ['_name_edits', '_name_labels init', 'Name row', 'TextItem', '_on_name_changed', 'setPos']:
    if k == '_name_labels init':
        print(k + ':', '_name_labels = []' in c, flush=True)
    else:
        print(k + ':', k in c, flush=True)
