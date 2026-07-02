import base64, zlib, sys, os

# The full wave_panel.py content (base64 encoded to avoid encoding issues)
# I'll write it in multiple parts

TARGET = r'D:\test\STM32H723ZGT6\host\widgets\wave_panel.py'

class_part = '''
class WaveViewBox(pg.ViewBox):
    def wheelEvent(self, ev, axis=None):
        if ev.modifiers() & Qt.KeyboardModifier.ControlModifier:
            pg.ViewBox.wheelEvent(self, ev, axis=0)
        else:
            pg.ViewBox.wheelEvent(self, ev, axis=1)
        ev.accept()
'''

# Write the class
with open(TARGET, 'a', encoding='utf-8') as f:
    f.write(class_part)
print('Class written')
