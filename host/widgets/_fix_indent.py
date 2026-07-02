import re

with open(r'D:\\test\\STM32H723ZGT6\\host\\widgets\\wave_panel.py', 'r', encoding='utf-8') as f:
    content = f.read()

old = '''        return x, y

   def _do_update_plot(self):
        n = self.table.rowCount()

       if n == 0:
           for curve in self._curves:
               curve.setData([], [])
           self._clear_clk_markers()
           return

       ch_states = []
       for c in range(NUM_CH):
            arr = np.array([
                1 if (self.table.item(r, c) is not None
                      and self.table.item(r, c).text().strip() == '1')
                else 0
                for r in range(n)
            ], dtype=np.uint8)
           ch_states.append(arr)'''

new = '''        return x, y

    def _do_update_plot(self):
        n = self.table.rowCount()

        if n == 0:
            for curve in self._curves:
                curve.setData([], [])
            self._clear_clk_markers()
            return

        ch_states = []
        for c in range(NUM_CH):
            arr = np.array([
                1 if (self.table.item(r, c) is not None
                      and self.table.item(r, c).text().strip() == '1')
                else 0
                for r in range(n)
            ], dtype=np.uint8)
            ch_states.append(arr)'''

if old in content:
    content = content.replace(old, new)
    with open(r'D:\\test\\STM32H723ZGT6\\host\\widgets\\wave_panel.py', 'w', encoding='utf-8') as f:
        f.write(content)
    print('Fixed indentation')
else:
    print('Pattern not found, searching for def line...')
    lines = content.split('\n')
    for i, line in enumerate(lines):
        if 'def _do_update_plot' in line:
            print(f'Line {i+1}: {repr(line)}')
