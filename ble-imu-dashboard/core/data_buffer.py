import numpy as np
from collections import deque

class DataBuffer:
    """Ring buffer for realtime plotting."""
    def __init__(self, maxlen=2000):
        self.time = deque(maxlen=maxlen)
        self.values = deque(maxlen=maxlen)

    def append(self, t, v):
        self.time.append(t)
        self.values.append(v)

    def to_numpy(self):
        return np.array(self.time), np.array(self.values)
