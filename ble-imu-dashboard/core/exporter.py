import pandas as pd
from datetime import datetime

class DataExporter:
    """Export sensor data to CSV files."""
    
    def __init__(self):
        self.data = []
    
    def add_sample(self, timestamp: float, x: float, y: float, z: float):
        """Add a data sample."""
        self.data.append({
            'timestamp': timestamp,
            'x': x,
            'y': y,
            'z': z
        })
    
    def export_csv(self, filename: str = None):
        """Export data to CSV file."""
        if not filename:
            filename = f"imu_data_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        
        df = pd.DataFrame(self.data)
        df.to_csv(filename, index=False)
        print(f"[Export] Saved {len(self.data)} samples to {filename}")
        return filename
    
    def clear(self):
        """Clear all stored data."""
        self.data.clear()
