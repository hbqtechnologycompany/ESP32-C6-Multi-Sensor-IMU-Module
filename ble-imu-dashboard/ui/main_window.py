from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QPushButton, QLabel, QListWidget, QListWidgetItem,
    QMessageBox, QGroupBox, QRadioButton
)
from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QFont, QPixmap
from core.ble_client import BLEHandler, DeviceInfo
from core.esp32_parser import ESP32FrameParser
from .plot_widget import PlotWidget
import asyncio
from pathlib import Path

class IMUDashboard(QMainWindow):
    def __init__(self, loop: asyncio.AbstractEventLoop):
        super().__init__()
        self.loop = loop
        self.setWindowTitle("ESP32-C6 Multi-Sensor IMU Dashboard")
        self.resize(1600, 900)

        # --- BLE core
        self.ble = BLEHandler(self.on_ble_data, preferred_services=["1815"])
        self.esp32_parser = ESP32FrameParser()
        
        # Theme management
        self.current_theme = "Dark"
        self.ui_dir = Path(__file__).parent

        # --- UI layout
        # Top bar - All controls horizontal
        top_bar = QWidget()
        top_layout = QHBoxLayout(top_bar)
        
        # BLE Control buttons
        self.btn_scan = QPushButton("🔍 Scan")
        self.btn_connect = QPushButton("🔗 Connect")
        self.btn_disconnect = QPushButton("❌ Disconnect")
        
        # Device list (compact)
        device_label = QLabel("📱 Device:")
        self.list_devices = QListWidget()
        self.list_devices.setMaximumHeight(80)
        self.list_devices.setMaximumWidth(300)
        
        # Data Stream buttons
        stream_label = QLabel("📡 Stream:")
        self.btn_start = QPushButton("▶ Start")
        self.btn_stop = QPushButton("⏹ Stop")
        self.btn_ClearPlot = QPushButton("Clear Plot")
        
        # Status & Stats
        self.lbl_status = QLabel("Status: Idle")
        self.lbl_status.setFont(QFont("Segoe UI", 9, QFont.Weight.Bold))
        self.lbl_stats = QLabel("📈 0 frames")
        self.lbl_stats.setFont(QFont("Segoe UI", 9))
        
        # Theme selector
        theme_group = QGroupBox("Theme")
        theme_layout = QHBoxLayout()
        self.radio_light = QRadioButton("☀")
        self.radio_dark = QRadioButton("🌙")
        self.radio_dark.setChecked(True)
        theme_layout.addWidget(self.radio_light)
        theme_layout.addWidget(self.radio_dark)
        theme_layout.setContentsMargins(2, 2, 2, 2)
        theme_group.setLayout(theme_layout)
        theme_group.setMaximumWidth(100)
        
        # Company Logo
        logo_label = QLabel()
        logo_path = self.ui_dir / "imgs" / "logo-HBQ-1.png"
        if logo_path.exists():
            pixmap = QPixmap(str(logo_path))
            # Scale logo to fit (max height 60px, keep aspect ratio)
            scaled_pixmap = pixmap.scaledToHeight(60, Qt.TransformationMode.SmoothTransformation)
            logo_label.setPixmap(scaled_pixmap)
            logo_label.setToolTip("HBQ Company")
        else:
            logo_label.setText("🏢 HBQ")
            logo_label.setFont(QFont("Segoe UI", 12, QFont.Weight.Bold))
        
        # Add all to top bar
        top_layout.addWidget(self.btn_scan)
        top_layout.addWidget(self.btn_connect)
        top_layout.addWidget(self.btn_disconnect)
        top_layout.addWidget(device_label)
        top_layout.addWidget(self.list_devices)
        top_layout.addWidget(stream_label)
        top_layout.addWidget(self.btn_start)
        top_layout.addWidget(self.btn_stop)
        top_layout.addWidget(self.btn_ClearPlot)
        top_layout.addStretch(1)
        top_layout.addWidget(self.lbl_status)
        top_layout.addWidget(self.lbl_stats)
        top_layout.addWidget(theme_group)
        top_layout.addWidget(logo_label)

        # Main panel - Grid of 12 plots (3 rows × 4 columns)
        # Rows: X, Y, Z axes
        # Columns: IIS3DWB (g), ICM_Gyro (dps), Magnetometer (mG), SCL_Angle (deg)
        plot_panel = QWidget()
        plot_layout = QGridLayout(plot_panel)
        plot_layout.setSpacing(3)
        
        # Create 12 plots (4 sensors × 3 axes)
        self.plot_iis3dwb_x = PlotWidget("IIS3DWB X (g)", 'r', maxlen=1000)
        self.plot_iis3dwb_y = PlotWidget("IIS3DWB Y (g)", 'g', maxlen=1000)
        self.plot_iis3dwb_z = PlotWidget("IIS3DWB Z (g)", 'b', maxlen=1000)
        
        self.plot_gyro_x = PlotWidget("Gyro X (dps)", 'r', maxlen=1000)
        self.plot_gyro_y = PlotWidget("Gyro Y (dps)", 'g', maxlen=1000)
        self.plot_gyro_z = PlotWidget("Gyro Z (dps)", 'b', maxlen=1000)
        
        self.plot_mag_x = PlotWidget("Mag X (mG)", 'r', maxlen=1000)
        self.plot_mag_y = PlotWidget("Mag Y (mG)", 'g', maxlen=1000)
        self.plot_mag_z = PlotWidget("Mag Z (mG)", 'b', maxlen=1000)
        
        self.plot_angle_x = PlotWidget("Angle X (deg)", 'r', maxlen=1000)
        self.plot_angle_y = PlotWidget("Angle Y (deg)", 'g', maxlen=1000)
        self.plot_angle_z = PlotWidget("Angle Z (deg)", 'b', maxlen=1000)
        
        # Add plots to grid (3 rows × 4 columns)
        # Row 0 (X-axis)
        plot_layout.addWidget(self.plot_iis3dwb_x, 0, 0)
        plot_layout.addWidget(self.plot_gyro_x, 0, 1)
        plot_layout.addWidget(self.plot_mag_x, 0, 2)
        plot_layout.addWidget(self.plot_angle_x, 0, 3)
        
        # Row 1 (Y-axis)
        plot_layout.addWidget(self.plot_iis3dwb_y, 1, 0)
        plot_layout.addWidget(self.plot_gyro_y, 1, 1)
        plot_layout.addWidget(self.plot_mag_y, 1, 2)
        plot_layout.addWidget(self.plot_angle_y, 1, 3)
        
        # Row 2 (Z-axis)
        plot_layout.addWidget(self.plot_iis3dwb_z, 2, 0)
        plot_layout.addWidget(self.plot_gyro_z, 2, 1)
        plot_layout.addWidget(self.plot_mag_z, 2, 2)
        plot_layout.addWidget(self.plot_angle_z, 2, 3)

        # Main layout
        root = QWidget()
        root_layout = QVBoxLayout(root)
        root_layout.addWidget(top_bar)
        root_layout.addWidget(plot_panel)
        self.setCentralWidget(root)
        
        # Apply initial theme
        self.apply_theme("light")

        # --- signals
        self.btn_scan.clicked.connect(lambda: asyncio.ensure_future(self.do_scan()))
        self.btn_connect.clicked.connect(lambda: asyncio.ensure_future(self.do_connect()))
        self.btn_disconnect.clicked.connect(lambda: asyncio.ensure_future(self.do_disconnect()))
        self.btn_start.clicked.connect(lambda: asyncio.ensure_future(self.do_start()))
        self.btn_stop.clicked.connect(lambda: asyncio.ensure_future(self.ble.stop_notify()))
        self.btn_ClearPlot.clicked.connect(self.clear_all_plots)
        
        # Theme signals
        self.radio_light.toggled.connect(lambda checked: self.apply_theme("light") if checked else None)
        self.radio_dark.toggled.connect(lambda checked: self.apply_theme("dark") if checked else None)

        self.devices: list[DeviceInfo] = []
        self.parser = None

        # Refresh timer for plots
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_plots)
        self.timer.start(30)
        
        # Stats timer
        self.stats_timer = QTimer(self)
        self.stats_timer.timeout.connect(self.update_stats)
        self.stats_timer.start(1000)

    # ========== BLE Actions ==========
    async def do_scan(self):
        self.set_status("🔍 Scanning...", "scanning")
        try:
            self.devices = await self.ble.scan()
            self.list_devices.clear()
            for d in self.devices:
                self.list_devices.addItem(QListWidgetItem(f"{d.name} [{d.address}] RSSI:{d.rssi}"))
            self.set_status(f"Found {len(self.devices)} devices", "normal")
        except Exception as e:
            self.error(f"Scan failed: {e}")

    async def do_connect(self):
        row = self.list_devices.currentRow()
        if row < 0:
            self.error("Select a device first.")
            return
        dev = self.devices[row]
        try:
            await self.ble.connect(dev.address)
            self.set_status(f"✅ Connected: {dev.name}", "success")
        except Exception as e:
            self.error(f"Connect failed: {e}")

    async def do_disconnect(self):
        await self.ble.disconnect()
        self.set_status("⚪ Disconnected", "normal")

    async def do_start(self):
        """Start notify on characteristic 0x2A58 (ESP32-C6 IMU data)"""
        if not self.ble.client or not self.ble.client.is_connected:
            self.error("Not connected to device.")
            return
        
        try:
            # ESP32-C6 uses characteristic UUID 0x2A58 in service 0x1815
            await self.ble.start_notify("00002a58-0000-1000-8000-00805f9b34fb")
            self.set_status("📡 Streaming...", "streaming")
        except Exception as e:
            self.error(f"Start notify failed: {e}")

    # ========== UI Management ==========
    def apply_theme(self, theme: str):
        """Apply light or dark theme"""
        theme = theme.lower()  # Make case-insensitive
        self.current_theme = theme
        
        if theme == "light":
            qss_file = self.ui_dir / "style_light.qss"
        else:
            qss_file = self.ui_dir / "style_dark.qss"
        
        try:
            with open(qss_file, 'r', encoding='utf-8') as f:
                self.setStyleSheet(f.read())
            print(f"🎨 Theme changed to: {theme}")
        except Exception as e:
            print(f"❌ Failed to load theme: {e}")

    def set_status(self, msg: str, status_type: str = "normal"):
        """Set status message with color coding
        
        Args:
            msg: Status message to display
            status_type: Type of status - "normal", "success", "streaming", "scanning", "error"
        """
        self.lbl_status.setText(f"Status: {msg}")
        
        # Apply color based on status type
        if status_type == "success":
            self.lbl_status.setStyleSheet("color: #00ff00; font-weight: bold;")  # Green
        elif status_type == "streaming":
            self.lbl_status.setStyleSheet("color: #00bfff; font-weight: bold;")  # Blue
        elif status_type == "scanning":
            self.lbl_status.setStyleSheet("color: #ffaa00; font-weight: bold;")  # Orange
        elif status_type == "error":
            self.lbl_status.setStyleSheet("color: #ff0000; font-weight: bold;")  # Red
        else:  # normal
            self.lbl_status.setStyleSheet("")  # Default theme color

    def error(self, msg: str):
        self.set_status(f"❌ {msg}", "error")
        QMessageBox.critical(self, "Error", msg)

    def refresh_plots(self):
        """Refresh all 12 plots"""
        self.plot_iis3dwb_x.refresh()
        self.plot_iis3dwb_y.refresh()
        self.plot_iis3dwb_z.refresh()
        
        self.plot_gyro_x.refresh()
        self.plot_gyro_y.refresh()
        self.plot_gyro_z.refresh()
        
        self.plot_mag_x.refresh()
        self.plot_mag_y.refresh()
        self.plot_mag_z.refresh()
        
        self.plot_angle_x.refresh()
        self.plot_angle_y.refresh()
        self.plot_angle_z.refresh()

    def clear_all_plots(self):
        """Clear/Reset all 12 plots"""
        self.plot_iis3dwb_x.reset()
        self.plot_iis3dwb_y.reset()
        self.plot_iis3dwb_z.reset()
        
        self.plot_gyro_x.reset()
        self.plot_gyro_y.reset()
        self.plot_gyro_z.reset()
        
        self.plot_mag_x.reset()
        self.plot_mag_y.reset()
        self.plot_mag_z.reset()
        
        self.plot_angle_x.reset()
        self.plot_angle_y.reset()
        self.plot_angle_z.reset()
        
        print("🧹 All plots cleared!")
        self.set_status("🧹 Plots cleared", "normal")

    def update_stats(self):
        """Update statistics display"""
        frame_count = self.esp32_parser.frame_count
        error_count = self.esp32_parser.error_count
        self.lbl_stats.setText(f"📈 {frame_count} frames, {error_count} errors")

    # ========== Data path ==========
    def on_ble_data(self, data: bytes):
        """Parse ESP32-C6 IMU BLE frame and extract sensor data"""
        result = self.esp32_parser.parse(data)
        
        if result is None:
            return  # Parse error
        
        header, sensor_data = result
        
        # Plot all available sensors in this frame
        # IIS3DWB Accelerometer - bit 0 (0x0001)
        if header.sensor_mask & 0x0001:
            self.plot_iis3dwb_x.append(sensor_data.iis3dwb_accel_x)
            self.plot_iis3dwb_y.append(sensor_data.iis3dwb_accel_y)
            self.plot_iis3dwb_z.append(sensor_data.iis3dwb_accel_z)
        
        # ICM Gyroscope - bit 2 (0x0004)
        if header.sensor_mask & 0x0004:
            self.plot_gyro_x.append(sensor_data.gyro_x)
            self.plot_gyro_y.append(sensor_data.gyro_y)
            self.plot_gyro_z.append(sensor_data.gyro_z)
        
        # Magnetometer - bit 4 (0x0010)
        if header.sensor_mask & 0x0010:
            self.plot_mag_x.append(sensor_data.mag_x)
            self.plot_mag_y.append(sensor_data.mag_y)
            self.plot_mag_z.append(sensor_data.mag_z)
        
        # SCL3300 Inclinometer - bit 6 (0x0040)
        if header.sensor_mask & 0x0040:
            self.plot_angle_x.append(sensor_data.angle_x)
            self.plot_angle_y.append(sensor_data.angle_y)
            self.plot_angle_z.append(sensor_data.angle_z)
        
        # Debug: Print detailed info every 50 frames
        if header.sequence % 50 == 0:
            available_sensors = []
            if header.sensor_mask & 0x0001: available_sensors.append("IIS3DWB")
            if header.sensor_mask & 0x0002: available_sensors.append("ICM_Accel")
            if header.sensor_mask & 0x0004: available_sensors.append("ICM_Gyro")
            if header.sensor_mask & 0x0010: available_sensors.append("Mag")
            if header.sensor_mask & 0x0040: available_sensors.append("SCL_Angle")
            if header.sensor_mask & 0x0080: available_sensors.append("SCL_Accel")
            
            print(f"📊 Frame #{header.sequence}: Mask=0x{header.sensor_mask:04X}")
            print(f"   Available: {', '.join(available_sensors) if available_sensors else 'NONE'}")
