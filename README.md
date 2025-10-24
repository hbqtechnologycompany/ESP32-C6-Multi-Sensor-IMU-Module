# ESP32-C6 Multi Sensor IMU Measurement Module

> [VI] Mô-đun đo IMU đa cảm biến trên ESP32‑C6 với giám sát WebSocket realtime và BLE (tùy chọn). Hỗ trợ IIS2MDC (từ kế), IIS3DWB (gia tốc tốc độ cao), ICM45686 (IMU 6 trục), SCL3300 (inclinometer). Các mục song ngữ có nhãn [VI].

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/YOUR_USERNAME/ESP32_Vibra_Accel_inclio_Module)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.4-blue.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![Platform](https://img.shields.io/badge/platform-ESP32--C6-red.svg)](https://www.espressif.com/en/products/socs/esp32-c6)

[![Donate via PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://paypal.me/hbqtechnology)
[![GitHub Sponsors](https://img.shields.io/github/sponsors/hbqtechnologycompany?style=social)](https://github.com/sponsors/hbqtechnologycompany)

A comprehensive high-quality IMU measurement system with web-based monitoring capabilities, featuring multiple sensors for various applications.
![ESP32-C6-Multi-Sensor-IMU-Module](https://github.com/user-attachments/assets/25552b5a-b936-4bbb-9c8f-0b7d65bbd243)

## 🎯 Features

[VI] Tính năng

- **Multi-Sensor Support**: IIS2MDC (Magnetometer), IIS3DWB (High-speed Accelerometer), ICM45686 (6-axis IMU), SCL3300 (Inclinometer)
- **High-Speed Data Collection**: Up to 26.7kHz sampling rate
- **Multiple Firmware Options**: Web monitoring, BLE streaming, high-speed monitoring
- **Python BLE Dashboard**: Standalone GUI for real-time data visualization
- **Web-Based Monitoring**: Real-time data visualization and control
- **BLE 5.0 Streaming**: High-speed data transmission with 2M PHY
- **FIFO Buffering**: Prevents data loss during high-speed operations
- **APEX Features**: Advanced motion detection and gesture recognition
- **Multiple Communication Protocols**: I2C and SPI support
- **ESP32-C6 Platform**: WiFi 6 and Bluetooth 5.0 connectivity
- **Standalone Executable**: No Python installation required for dashboard

## 📊 Sensor Specifications

| Sensor | Type | Communication | Max Sample Rate | Features |
|--------|------|---------------|-----------------|----------|
| IIS2MDC | Magnetometer | I2C | 100Hz | Temperature compensation |
| IIS3DWB | Accelerometer | SPI | 26.7kHz | FIFO, High-speed |
| ICM45686 | 6-axis IMU | SPI | 100Hz | APEX features, Gesture recognition |
| SCL3300 | Inclinometer | SPI | 1kHz | CRC protection, 4 modes |

## 🚀 Quick Start

[VI] Bắt đầu nhanh

### Available Firmware Options

[VI] Các tùy chọn firmware

1. **ESP32C6_IMU_WebMonitor**: Unified WiFi + BLE monitoring with web interface
2. **ESP32C6_IMU_BLEStreamer**: BLE-only streaming for mobile/desktop apps
3. **ESP32C6_IIS3_WebMonitor_HighSpeed**: High-speed web monitoring for IIS3DWB
4. **Individual Sensor Tests**: Test each sensor separately
5. **Python BLE Dashboard**: Desktop GUI for BLE data visualization

### Prerequisites
- ESP-IDF v5.4 or later
- ESP32-C6 development board
- Required sensors (IIS2MDC, IIS3DWB, ICM45686, SCL3300)
- Python 3.11+ (for BLE dashboard)

### Installation

1. **Clone the repository**:
```bash
git clone https://github.com/hbqtechnologycompany/ESP32-C6-Multi-Sensor-IMU-Module.git
cd ESP32-C6-Multi-Sensor-IMU-Module
```

2. **Set up ESP-IDF**:
```bash
# Install ESP-IDF v5.4
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.4
./install.sh esp32c6
. ./export.sh
```

3. **Build and flash** (choose your preferred firmware):
```bash
# For Web-based monitoring (WiFi + BLE)
cd ESP32C6_IMU_WebMonitor
idf.py build
idf.py flash monitor

# For BLE-only streaming
cd ESP32C6_IMU_BLEStreamer
idf.py build
idf.py flash monitor

# For high-speed web monitoring
cd ESP32C6_IIS3_WebMonitor_HighSpeed
idf.py build
idf.py flash monitor
```

## 📁 Project Structure

[VI] Cấu trúc dự án

```
ESP32-C6-Multi-Sensor-IMU-Module/
├── ESP32C6_IIS2MDC Test/           # Magnetometer test example
├── ESP32C6_IIS3DWBTR Test/         # High-speed accelerometer test
├── ESP32C6_IIS3_WebMonitor_HighSpeed/  # High-speed web monitoring
├── ESP32C6_IMU_BLEStreamer/        # BLE streaming firmware
├── ESP32C6_IMU_WebMonitor/         # Web-based monitoring system
├── icm45686 test/                   # 6-axis IMU test with APEX features
├── SCL3300 Test/                    # Inclinometer test example
├── ble-imu-dashboard/               # Python GUI for BLE data visualization
├── docs/                            # Documentation files
├── Schematic/                       # Hardware schematics
├── DETAILED_GUIDE.md               # Comprehensive documentation
├── CONTRIBUTING.md                  # Contribution guidelines
└── README.md                       # This file
```

## 🌐 Monitoring Interfaces

[VI] Giao diện giám sát

### Web-based Monitoring
The web monitoring system provides:
- **Real-time Data Visualization**: Live charts and graphs
- **REST API**: JSON endpoints for data access
- **WebSocket Support**: Real-time data streaming
- **Configuration Interface**: Remote sensor configuration
- **Data Export**: CSV and JSON download options

### Python BLE Dashboard
A standalone Python GUI application for BLE data visualization:
- **Auto-scan**: Automatic ESP32-C6 device discovery
- **Real-time Streaming**: BLE NOTIFY data reception
- **12-plot Grid**: 4 sensors × 3 axes (X/Y/Z) visualization
- **Light/Dark Themes**: Comfortable viewing experience
- **Live Statistics**: Frame count and error tracking
- **Standalone Executable**: No Python installation required

### Using the Python BLE Dashboard

[VI] Sử dụng Python BLE Dashboard

1. **Install Python dependencies**:
```bash
cd ble-imu-dashboard
pip install -r requirements.txt
```

2. **Run the dashboard**:
```bash
# Windows PowerShell
.\run.ps1

# Windows Batch
run.bat

# Manual run
python main.py
```

3. **Build standalone executable**:
```bash
.\build_exe.ps1
# Output: dist\ESP32-IMU-Dashboard.exe
```

### API Endpoints

[VI] API chính

- `GET /api/data` - Latest sensor data
- `GET /api/stats` - Buffer statistics
- `POST /api/config` - Configuration changes
- `GET /api/download?format=csv` - Data export

## 🔧 Configuration

[VI] Cấu hình

### GPIO Configuration

| Sensor | SDA/SDI | SCL/SCK | CS | INT |
|--------|---------|---------|----|----|
| IIS2MDC | GPIO23 | GPIO22 | - | - |
| IIS3DWB | GPIO7 | GPIO6 | GPIO19 | - |
| ICM45686 | GPIO7 | GPIO6 | GPIO5 | - |
| SCL3300 | GPIO7 | GPIO6 | GPIO20 | - |

### WiFi Configuration

Update WiFi credentials in `main.c`:
```c
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
```

## 📈 Performance Optimization

[VI] Tối ưu hiệu năng

- **DMA Usage**: All SPI transactions use DMA for maximum throughput
- **FIFO Management**: Smart watermark configuration prevents overflow
- **Task Priorities**: Optimized FreeRTOS task scheduling
- **Memory Management**: Efficient buffer management with circular buffers

## 🎯 Applications

[VI] Ứng dụng

### Industrial Monitoring
- Vibration analysis for machinery
- Structural health monitoring
- Tilt measurement for construction

### IoT and Wearables
- Fitness tracking
- Gesture recognition
- Motion detection

### Research and Development
- Motion analysis
- Sensor fusion algorithms
- Machine learning training data

## 🧪 Individual Sensor Testing

[VI] Kiểm tra từng cảm biến

For testing individual sensors or learning sensor-specific implementations:

- **ESP32C6_IIS2MDC Test**: Magnetometer testing and calibration
- **ESP32C6_IIS3DWBTR Test**: High-speed accelerometer testing
- **icm45686 test**: 6-axis IMU with APEX features testing
- **SCL3300 Test**: Inclinometer testing and angle measurement

Each test project includes:
- Sensor-specific drivers and examples
- Configuration and calibration routines
- Basic data logging and visualization
- Troubleshooting guides

## 🔧 Troubleshooting

[VI] Xử lý sự cố

### Common Issues

1. **I2C Communication Errors**
   - Check pullup resistors (4.7kΩ recommended)
   - Verify wiring connections
   - Reduce I2C clock speed if needed

2. **SPI Data Corruption**
   - Check clock polarity and phase settings
   - Verify CS signal timing
   - Ensure proper grounding

3. **FIFO Overflow**
   - Increase data processing frequency
   - Reduce FIFO watermark
   - Optimize data processing algorithms

4. **WiFi Connection Issues**
   - Check WiFi credentials
   - Verify signal strength
   - Check firewall settings

## 📚 Documentation

[VI] Tài liệu

- [Detailed Guide](DETAILED_GUIDE.md) - Comprehensive setup and usage guide
- [BLE Viewer Guide](docs/BLE_Viewer_Guide.md) - BLE dashboard usage guide
- [Contributing Guidelines](CONTRIBUTING.md) - How to contribute to the project
- [Hardware Schematic](Schematic/ESP32_IMU_Module.pdf) - PCB and wiring information
- [Individual Project READMEs](ESP32C6_IMU_WebMonitor/README.md) - Specific firmware documentation

## 🤝 Contributing

[VI] Đóng góp

We welcome contributions! Please see our [Contributing Guidelines](CONTRIBUTING.md) for details.

### Development Setup
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## 📄 License

[VI] Giấy phép

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 💰 Support & Purchase

[VI] Hỗ trợ & Mua hàng

### Buy ESP32-C6-Multi-Sensor-IMU-Module
**Order Link**: [HBQ Technology Store](https://store.hbqsolution.com/?product=esp32-c6-multi-sensor-imu-module)

**Contact Information**:
- Email: contact@hbqsolution.com | hbqsolution@gmail.com
- Phone: (+84) 035 719 1643 | (+84) 094 850 7979
- Address: 31, Đường số 8, Cityland Garden Hill, P. An Nhơn, TP HCM

### Support the Project
- **PayPal Donate**: [Donate via PayPal](https://paypal.me/hbqtechnology)
- **GitHub Sponsors**: [Support on GitHub](https://github.com/sponsors/hbqtechnologycompany)

## 🙏 Acknowledgments

- ESP-IDF team for the excellent framework
- STMicroelectronics for sensor documentation
- InvenSense for ICM45686 driver
- Murata for SCL3300 support
- HBQ Technology for hardware support

## 📞 Contact

- **Issues**: [GitHub Issues](https://github.com/hbqtechnologycompany/ESP32-C6-Multi-Sensor-IMU-Module/issues)
- **Discussions**: [GitHub Discussions](https://github.com/hbqtechnologycompany/ESP32-C6-Multi-Sensor-IMU-Module/discussions)
- **Email**: hbqsolution@gmail.com

---

**Made with ❤️ for the IoT and embedded systems community**
