import pyqtgraph as pg
from PyQt6.QtWidgets import QWidget, QVBoxLayout
from time import perf_counter
from collections import deque
import numpy as np

class PlotWidget(QWidget):
    """Realtime plot widget for one sensor axis."""
    def __init__(self, title="Sensor Stream", color='y', maxlen=500):
        super().__init__()
        layout = QVBoxLayout(self)
        self.plot = pg.PlotWidget(title=title)
        layout.addWidget(self.plot)
        self.curve = self.plot.plot(pen=color)
        self.data_x = deque(maxlen=maxlen)
        self.data_y = deque(maxlen=maxlen)
        self.t0 = perf_counter()

    def append(self, value: float):
        t = perf_counter() - self.t0
        self.data_x.append(t)
        self.data_y.append(value)

    def refresh(self):
        if not self.data_x:
            return
        self.curve.setData(np.fromiter(self.data_x, float),
                           np.fromiter(self.data_y, float))

    def reset(self):
        """Reset plot data and timer"""
        self.data_x.clear()
        self.data_y.clear()
        self.t0 = perf_counter()
        self.curve.clear()
