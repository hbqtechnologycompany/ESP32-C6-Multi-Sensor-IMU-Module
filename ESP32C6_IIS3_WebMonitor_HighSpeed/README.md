# ESP32-C6 IIS3DWB High-Speed Web Monitor

> [VI] Hệ thống giám sát web cao cấp cho cảm biến gia tốc tốc độ cao IIS3DWB trên ESP32-C6. Dashboard tích hợp sẵn, streaming realtime qua WebSocket với tốc độ 26.7 kHz.

A high-performance web-based monitoring system for the **IIS3DWB** high-speed accelerometer featuring real-time data visualization, WebSocket streaming, and comprehensive diagnostics running on **ESP32-C6**.

![Web Monitor Dashboard](imgs/webmonitor.png)

## 📋 Table of Contents

- [Features](#-features)
- [Supported Sensors](#-supported-sensors)
- [Quick Start](#-quick-start)
- [Web Interface](#-web-interface)
- [Configuration](#-configuration)
- [Performance Optimization](#-performance-optimization)
- [Monitoring and Debugging](#-monitoring-and-debugging)
- [Troubleshooting](#-troubleshooting)
- [Support & Purchase](#-support--purchase)
- [License](#-license)

## 🌟 Features

**[VI] Tính năng**

### 🚀 Real-time Performance
- **Ultra High-Speed Sampling**: 26.7 kHz ODR from IIS3DWB accelerometer
- **FIFO Burst Processing**: Smart watermark at 192 samples with DMA transfers
- **Low Latency WebSocket**: ~100 Hz broadcast rate with chunked data streaming
- **Efficient Buffering**: 6000-point circular buffer for smooth real-time plotting

### 🌐 Web Dashboard
- **Modern Light Theme UI**: Clean, professional interface with responsive design
- **Live Chart.js Visualization**: Real-time scrolling acceleration plots (X, Y, Z axes)
- **Comprehensive Metrics**: 
  - Plot points/sec & sensor samples/sec
  - FIFO level & batch size monitoring
  - Acceleration magnitude (|g|)
  - ODR (Output Data Rate) display
- **Event Logging**: WebSocket connection status and system events
- **Embedded Dashboard**: No SPIFFS required - dashboard served from firmware

### 📡 API & Streaming
- **RESTful API**: JSON endpoints for data access, stats, and configuration
- **WebSocket Streaming**: Low-latency push at `ws://<device-ip>/ws/data`
- **Data Export**: Download CSV or JSON formats for offline analysis
- **Compact JSON Protocol**: Optimized message format for high-speed transmission

### 🔧 Advanced Features
- **Multi-format Units**: Display in both g (gravity) and m/s²
- **Automatic Statistics**: Real-time calculation of throughput, buffer usage, and timing
- **Flexible Configuration**: Configurable via WiFi credentials in source code
- **Performance Monitoring**: Built-in metrics for message rate, sample rate, and FIFO health

## 🎯 Supported Sensors

**[VI] Cảm biến được hỗ trợ**

| Sensor | Type | Max Sample Rate | Interface | Key Features |
|--------|------|-----------------|-----------|--------------|
| **IIS3DWB** | High-speed Accelerometer | **26.7 kHz** | SPI | • 3-axis digital accelerometer<br>• ±2g/±4g/±8g/±16g programmable range<br>• 512-sample FIFO buffer<br>• Built-in anti-aliasing filter<br>• Vibration & impact monitoring<br>• Ultra-low noise density |

### IIS3DWB Specifications
- **Manufacturer**: STMicroelectronics
- **Communication**: SPI (up to 10 MHz)
- **Power Supply**: 1.7V - 3.6V
- **Current Consumption**: 1.1 mA (high-performance mode)
- **Temperature Range**: -40°C to +85°C
- **Package**: LGA-14L (2.5 x 3.0 x 0.86 mm)

## 🚀 Quick Start

**[VI] Bắt đầu nhanh**

### Prerequisites

**[VI] Yêu cầu**

- **ESP-IDF**: v5.4 or later ([Installation Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/))
- **Hardware**:
  - ESP32-C6 development board
  - IIS3DWB sensor module (SPI interface)
  - Jumper wires for connections
- **WiFi Network**: 2.4 GHz network access
- **Web Browser**: Chrome, Firefox, Edge, or Safari

### Installation

**[VI] Cài đặt**

#### 1. Clone Repository
```bash
git clone https://github.com/hbqtechnologycompany/ESP32-C6-Multi-Sensor-IMU-Module.git
cd ESP32-C6-Multi-Sensor-IMU-Module/ESP32C6_II3S_WebMonitor_HighSpeed
```

#### 2. Hardware Connection

Connect IIS3DWB to ESP32-C6 using SPI interface:

| IIS3DWB Pin | ESP32-C6 GPIO | Function |
|-------------|---------------|----------|
| MISO        | GPIO 2        | SPI MISO |
| MOSI        | GPIO 7        | SPI MOSI |
| SCK         | GPIO 6        | SPI Clock |
| CS          | GPIO 19       | Chip Select |
| VDD         | 3.3V          | Power |
| GND         | GND           | Ground |

> **Note**: If your hardware uses different pins, update them in `main/imu_manager.c`

#### 3. Configure WiFi

Edit `main/main.c` and update your WiFi credentials:
```c
// Line 22-23
#define WIFI_SSID      "Your_WiFi_SSID"
#define WIFI_PASS      "Your_WiFi_Password"
```

#### 4. Build and Flash

```bash
# Set up ESP-IDF environment (if not already done)
. $HOME/esp/esp-idf/export.sh

# Configure project (optional)
idf.py menuconfig

# Build firmware
idf.py build

# Flash to ESP32-C6
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
```

> **Tip**: Use `idf.py -p /dev/ttyUSB0 flash monitor` to flash and monitor in one command

#### 5. Access Web Interface

1. Wait for ESP32-C6 to connect to WiFi
2. Check serial monitor for IP address:
   ```
   I (5234) MAIN: got ip:192.168.1.100
   ```
3. Open browser and navigate to: `http://192.168.1.100/`
4. You should see the real-time dashboard with live acceleration plots!

### First Run Checklist

**[VI] Kiểm tra lần chạy đầu**

- [ ] ESP32-C6 connected to computer via USB
- [ ] IIS3DWB properly wired to ESP32-C6
- [ ] WiFi credentials updated in code
- [ ] Firmware flashed successfully
- [ ] ESP32-C6 connected to WiFi (check serial monitor)
- [ ] Web dashboard accessible from browser
- [ ] Real-time data streaming visible on chart

## 🌐 Web Interface

**[VI] Giao diện Web**

### Dashboard Overview

The embedded web dashboard provides a professional, **light-themed interface** for real-time IMU monitoring with the following components:

#### 📊 Real-time Visualization
- **Scrolling Chart**: Live acceleration plot with 200-point rolling window
  - **X-axis**: Red line (#ef4444)
  - **Y-axis**: Green line (#10b981)
  - **Z-axis**: Blue line (#3b82f6)
- **Chart.js 4**: High-performance canvas rendering with animation disabled for smooth updates
- **Auto-scaling**: Y-axis automatically adjusts to data range
- **Update Rate**: ~100 Hz WebSocket push with ~10 samples per message

#### 📈 Live Metrics Dashboard

Six key metrics displayed in grid layout:

| Metric | Description | Unit |
|--------|-------------|------|
| **Plot pts/s** | Points rendered per second on chart | pts/s |
| **Sensor samples/s** | Actual sampling rate from IIS3DWB | samples/s |
| **FIFO level** | Current FIFO depth | samples |
| **Batch size** | Samples per batch read | samples |
| **\|g\|** | Acceleration magnitude | g |
| **ODR (Hz)** | Output Data Rate configuration | Hz |

#### 📝 Event Log
- WebSocket connection status
- System events and errors
- Performance statistics (updated every 200 messages)
- Scrollable log with 160 entry history

### API Endpoints

**[VI] API Endpoints**

#### 1. Get Latest Data
```http
GET /api/data
```

**Response:**
```json
{
  "timestamp_us": 1234567890,
  "accelerometer_g": {
    "x_g": 0.012,
    "y_g": -0.023,
    "z_g": 0.985,
    "magnitude_g": 0.986
  },
  "accelerometer_ms2": {
    "x_ms2": 0.118,
    "y_ms2": -0.225,
    "z_ms2": 9.658,
    "magnitude_ms2": 9.667
  },
  "stats": {
    "fifo_level": 24,
    "samples_read": 64,
    "batch_interval_us": 2395,
    "samples_per_second": 26700,
    "plot_samples_per_second": 1000,
    "msg_per_second": 100,
    "odr_hz": 26700,
    "websocket_total_messages": 45678
  }
}
```

#### 2. Get Statistics
```http
GET /api/stats
```

**Response:**
```json
{
  "total_samples": 1234567,
  "dropped_samples": 0,
  "buffer_overflows": 0,
  "last_timestamp_us": 1234567890,
  "avg_processing_time_us": 125,
  "buffer_count": 500,
  "buffer_full": false,
  "buffer_empty": false,
  "imu_odr_hz": 26700,
  "imu_fifo_watermark": 192,
  "ws_msg_per_sec": 100.5,
  "ws_samples_per_sec": 1005.0,
  "ws_total_messages": 45678
}
```

#### 3. Get Configuration
```http
GET /api/config
```

**Response:**
```json
{
  "imu_odr_hz": 26700,
  "imu_fifo_watermark": 192
}
```

#### 4. Download Data

**CSV Format:**
```http
GET /api/download?format=csv
```

**JSON Format:**
```http
GET /api/download?format=json
```

Downloads the most recent 100 samples in the specified format.

### WebSocket Streaming

**[VI] WebSocket Streaming**

#### Connection
```javascript
const ws = new WebSocket('ws://192.168.1.100/ws/data');
```

#### Message Format
The WebSocket endpoint sends compact JSON messages at ~100 Hz:

```json
{
  "t": 1234567890,
  "chunks": {
    "x": [0.012, 0.013, 0.011, 0.014, 0.012, 0.015, 0.013, 0.014, 0.012, 0.011],
    "y": [-0.023, -0.024, -0.022, -0.023, -0.025, -0.024, -0.023, -0.022, -0.024, -0.023],
    "z": [0.985, 0.986, 0.984, 0.987, 0.985, 0.988, 0.986, 0.985, 0.987, 0.986]
  },
  "mag": 0.986,
  "s": {
    "fifo": 24,
    "batch": 64,
    "sps": 26700,
    "pps": 1000,
    "mps": 100,
    "odr": 26700,
    "chunk": 10
  }
}
```

**Field Descriptions:**
- `t`: Timestamp in microseconds
- `chunks.x/y/z`: Arrays of acceleration values in g
- `mag`: Magnitude of last sample in chunk
- `s.fifo`: Current FIFO level
- `s.batch`: Last batch size read
- `s.sps`: Sensor samples per second
- `s.pps`: Plot points per second
- `s.mps`: Messages per second
- `s.odr`: Configured ODR in Hz
- `s.chunk`: Number of samples in this message

## 🔧 Configuration

**[VI] Cấu hình**

### Hardware Configuration

**[VI] Cấu hình phần cứng**

#### SPI Pin Configuration

Default pin mapping in `main/imu_manager.c`:

```c
#define IIS3DWB_SPI_MISO   2    // GPIO 2
#define IIS3DWB_SPI_MOSI   7    // GPIO 7
#define IIS3DWB_SPI_CLK    6    // GPIO 6
#define IIS3DWB_SPI_CS     19   // GPIO 19
```

To change pins, modify these defines and rebuild:
```bash
idf.py build flash
```

#### SPI Speed
```c
#define IIS3DWB_SPI_FREQ_HZ    (8 * 1000 * 1000)  // 8 MHz
```

### Software Configuration

**[VI] Cấu hình phần mềm**

#### 1. WiFi Settings

In `main/main.c`:
```c
#define WIFI_SSID                   "Your_Network"
#define WIFI_PASS                   "Your_Password"
#define WIFI_MAXIMUM_RETRY          5
```

#### 2. IIS3DWB Sensor Configuration

In `main/imu_manager.c`:

**Output Data Rate (ODR):**
```c
// Fixed at maximum speed for high-performance monitoring
#define IIS3DWB_ODR    26700  // Hz (26.7 kHz)
```

**FIFO Watermark:**
```c
#define IIS3DWB_FIFO_WATERMARK    192  // samples
```

**Accelerometer Range:**
```c
// ±2g, ±4g, ±8g, or ±16g
// Default: ±2g for highest resolution
```

#### 3. Buffer Settings

In `main/data_buffer.h`:
```c
#define DATA_BUFFER_SIZE    1000    // Circular buffer capacity
```

#### 4. WebSocket Broadcast Settings

In `main/web_server.c`:
```c
#define WS_PLOT_CHUNK_SAMPLES      10    // Samples per WebSocket message
#define WS_PLOT_BUFFER_CAPACITY    6000  // Server-side ring buffer
```

**Broadcast Task Period:**
```c
TickType_t broadcast_period = pdMS_TO_TICKS(10);  // 10ms = ~100Hz
```

#### 5. Web Server Settings

In `main/web_server.h`:
```c
#define WEB_SERVER_PORT              80
#define WEB_SERVER_MAX_URI_HANDLERS  16
#define WEB_SERVER_STACK_SIZE        8192
#define WEBSOCKET_MAX_CONNECTIONS    4
```

#### 6. Chart Display Settings

In `main/web_server.c` (embedded HTML):
```javascript
const maxPoints = 200;  // Rolling window size for real-time display
```

Adjust this value:
- **Lower (50-100)**: More real-time, faster scrolling
- **Higher (500-1000)**: More history visible, slower scrolling

### Task Priorities

In `main/main.c`:
```c
#define IMU_TASK_PRIORITY           5    // Highest priority
#define WEB_SERVER_TASK_PRIORITY    4
#define DATA_PROCESSOR_PRIORITY     3
```

### Advanced Configuration via Menuconfig

```bash
idf.py menuconfig
```

Navigate to:
- **Component config → ESP-TLS** - Configure TLS if needed
- **Component config → HTTP Server** - Adjust httpd settings
- **Component config → Wi-Fi** - Fine-tune WiFi parameters
- **Component config → FreeRTOS** - Adjust RTOS settings

## 📊 Performance Optimization

**[VI] Tối ưu hiệu năng**

### System Architecture

```
IIS3DWB (26.7 kHz) 
    │ SPI DMA
    ▼
IMU Manager (Task Priority 5)
    │ FIFO Batch Read (192 samples)
    ▼
Circular Buffer (1000 samples)
    │
    ├──► REST API (/api/data, /api/stats)
    │
    └──► WebSocket Broadcast (~100 Hz)
             │ 10 samples/message
             ▼
         Web Dashboard
         (Chart.js 200-point rolling window)
```

### High-Speed Data Collection

#### 1. DMA-Based SPI Transfers
```c
// All SPI transactions use DMA for zero CPU overhead
spi_device_transmit(spi, &t);  // DMA transfer
```

#### 2. FIFO Management Strategy
- **Watermark**: 192 samples (optimal for batch reads)
- **Batch Read**: Up to 128 samples per interrupt
- **Efficient Processing**: Direct copy to circular buffer
- **Zero-Copy WebSocket**: Buffer shared between tasks

#### 3. FreeRTOS Task Optimization
```c
// High priority for IMU to prevent data loss
IMU_TASK_PRIORITY = 5           // Highest
WEB_SERVER_TASK_PRIORITY = 4    // High
DATA_PROCESSOR_PRIORITY = 3     // Medium
```

#### 4. Memory Management
- **Static Buffers**: Pre-allocated arrays (no malloc)
- **Circular Buffer**: Lock-free ring buffer with atomic operations
- **Stack Sizes**: Optimized for each task
  ```c
  IMU_TASK_STACK_SIZE = 8192
  WEB_SERVER_TASK_STACK_SIZE = 4096
  ```

### Web Server Optimization

#### 1. Embedded HTML Dashboard
- **No SPIFFS Required**: HTML served directly from firmware
- **Single Page**: ~7KB minified HTML/CSS/JS
- **Zero External Dependencies**: Except Chart.js CDN

#### 2. WebSocket Efficiency
```c
// Compact JSON format
{"t":1234567890,"chunks":{"x":[...],"y":[...],"z":[...]},"mag":0.986,"s":{...}}

// Typical message size: ~200-300 bytes for 10 samples
```

#### 3. Chart.js Optimization
```javascript
animation: false,              // Disable animations
pointRadius: 0,                // No point rendering
borderWidth: 1.5,              // Thin lines
chart.update('none')           // Skip animation frame
```

#### 4. Asynchronous WebSocket Send
```c
// Non-blocking async send
httpd_ws_send_frame_async(server, fd, &frame);
```

### Performance Metrics

**[VI] Chỉ số hiệu năng**

| Metric | Value | Description |
|--------|-------|-------------|
| **Sensor ODR** | 26,700 Hz | IIS3DWB sampling rate |
| **FIFO Watermark** | 192 samples | Batch size trigger |
| **SPI Speed** | 8 MHz | Communication clock |
| **WebSocket Rate** | ~100 Hz | Message broadcast frequency |
| **Points/Message** | 10 samples | WebSocket chunk size |
| **Chart Update** | ~1000 pts/s | UI rendering rate |
| **Buffer Capacity** | 1000 samples | Circular buffer |
| **Rolling Window** | 200 points | Chart display |
| **Message Latency** | ~10 ms | WebSocket round-trip |
| **CPU Usage** | ~40% | ESP32-C6 240MHz |
| **Memory Usage** | ~120 KB | Heap usage |

### Optimization Tips

**[VI] Mẹo tối ưu**

1. **Reduce Chart Points** for smoother scrolling:
   ```javascript
   const maxPoints = 100;  // Instead of 200
   ```

2. **Increase WebSocket Period** to reduce CPU load:
   ```c
   TickType_t broadcast_period = pdMS_TO_TICKS(20);  // 50Hz instead of 100Hz
   ```

3. **Decrease FIFO Watermark** for lower latency:
   ```c
   #define IIS3DWB_FIFO_WATERMARK 96  // Instead of 192
   ```

4. **Adjust Chunk Size** for different network conditions:
   ```c
   #define WS_PLOT_CHUNK_SAMPLES 5    // Smaller chunks = more frequent updates
   ```

## 🔍 Monitoring and Debugging

**[VI] Giám sát & Gỡ lỗi**

### Built-in Statistics

**[VI] Thống kê tích hợp**

Access via `/api/stats` endpoint:

```json
{
  "total_samples": 1234567,        // Total samples collected
  "dropped_samples": 0,            // Samples lost (should be 0)
  "buffer_overflows": 0,           // Buffer overflow count
  "last_timestamp_us": 1234567890, // Last sample timestamp
  "avg_processing_time_us": 125,   // Average processing time
  "buffer_count": 500,             // Current buffer usage
  "buffer_full": false,            // Buffer full flag
  "buffer_empty": false,           // Buffer empty flag
  "imu_odr_hz": 26700,            // Configured ODR
  "imu_fifo_watermark": 192,      // FIFO watermark
  "ws_msg_per_sec": 100.5,        // WebSocket message rate
  "ws_samples_per_sec": 1005.0,   // WebSocket sample rate
  "ws_total_messages": 45678      // Total messages sent
}
```

### Serial Monitor Logging

**[VI] Log Serial Monitor**

#### Enable Debug Logging

In `main/main.c`:
```c
// Set log level for all components
esp_log_level_set("*", ESP_LOG_INFO);

// Set log level for specific components
esp_log_level_set("WEB_SERVER", ESP_LOG_DEBUG);
esp_log_level_set("IMU_MANAGER", ESP_LOG_DEBUG);
esp_log_level_set("DATA_BUFFER", ESP_LOG_DEBUG);
```

#### Log Levels
```c
ESP_LOG_ERROR   // Critical errors only
ESP_LOG_WARN    // Warnings and errors
ESP_LOG_INFO    // General information (default)
ESP_LOG_DEBUG   // Detailed debug info
ESP_LOG_VERBOSE // Everything
```

#### Example Output
```
I (12345) IMU_MANAGER: FIFO read: 64 samples, level=24, took 245 us
I (12355) WEB_SERVER: WS broadcast: 45678 total sends, 2 active connections
I (12365) DATA_BUFFER: Buffer stats: 500/1000 used, 0 dropped
I (13000) WEB_SERVER: WS metrics: 100.2 msg/s, 1002 points/s
```

### Performance Monitoring

**[VI] Giám sát hiệu năng**

#### 1. Measure Operation Time
```c
int64_t start = esp_timer_get_time();
// Your operation here
int64_t end = esp_timer_get_time();
ESP_LOGI(TAG, "Operation took %lld us", end - start);
```

#### 2. Monitor Task Stack Usage
```bash
idf.py monitor
```
Look for stack high water marks in logs.

#### 3. Heap Memory Monitoring
```c
ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
ESP_LOGI(TAG, "Min free heap: %lu bytes", esp_get_minimum_free_heap_size());
```

#### 4. CPU Usage
Use FreeRTOS runtime stats:
```c
vTaskGetRunTimeStats(buffer);
ESP_LOGI(TAG, "Task stats:\n%s", buffer);
```

### Web Dashboard Event Log

The embedded dashboard includes a real-time event log showing:
- WebSocket connection status
- Data flow statistics (every 200 messages)
- Error messages and warnings
- Performance metrics

### Diagnostic Commands

**[VI] Lệnh chẩn đoán**

#### View All Logs
```bash
idf.py monitor
```

#### Filter Specific Component
```bash
idf.py monitor | grep "WEB_SERVER"
```

#### Save Logs to File
```bash
idf.py monitor > logs.txt
```

#### Check Partition Table
```bash
idf.py partition-table
```

#### Check Flash Size
```bash
idf.py size
```

### Health Indicators

**[VI] Chỉ số sức khỏe**

Monitor these metrics for system health:

| Indicator | Good | Warning | Critical |
|-----------|------|---------|----------|
| `dropped_samples` | 0 | 1-10 | >10 |
| `buffer_overflows` | 0 | 1-5 | >5 |
| `ws_msg_per_sec` | 95-105 | 80-95 | <80 |
| `avg_processing_time_us` | <200 | 200-500 | >500 |
| `free_heap` | >50KB | 20-50KB | <20KB |
| `fifo_level` | <180 | 180-192 | 192 (full) |

## 🛠️ Troubleshooting

**[VI] Khắc phục sự cố**

### Common Issues

**[VI] Các vấn đề thường gặp**

#### 1. WiFi Connection Failed

**[VI] Kết nối WiFi thất bại**

**Symptoms:**
```
E (5000) WIFI: connect to the AP fail
I (5000) MAIN: retry to connect to the AP
```

**Solutions:**
- ✅ Verify WiFi SSID and password in `main/main.c`
- ✅ Check if router is 2.4 GHz (ESP32-C6 doesn't support 5 GHz)
- ✅ Move ESP32-C6 closer to router
- ✅ Check router MAC filtering settings
- ✅ Try reducing `WIFI_MAXIMUM_RETRY` to 10 for longer retry period

**Test:**
```c
// Add more detailed WiFi logging
esp_log_level_set("wifi", ESP_LOG_VERBOSE);
```

---

#### 2. Sensor Initialization Failed

**[VI] Khởi tạo cảm biến thất bại**

**Symptoms:**
```
E (2000) IMU_MANAGER: Failed to initialize IIS3DWB
E (2000) IMU_MANAGER: SPI communication error
```

**Solutions:**
- ✅ Check physical wiring connections
- ✅ Verify GPIO pin numbers in `main/imu_manager.c`
- ✅ Ensure IIS3DWB has stable 3.3V power supply
- ✅ Check for short circuits or loose connections
- ✅ Measure voltage on VDD pin (should be 3.3V)
- ✅ Try reducing SPI speed:
  ```c
  #define IIS3DWB_SPI_FREQ_HZ (4 * 1000 * 1000)  // 4 MHz instead of 8 MHz
  ```

**Test:**
```bash
# Check for SPI errors in monitor
idf.py monitor | grep "SPI"
```

---

#### 3. Web Interface Not Loading

**[VI] Giao diện Web không tải**

**Symptoms:**
- Browser shows "Connection refused" or timeout
- Can't access `http://<device-ip>/`

**Solutions:**
- ✅ Check ESP32-C6 got IP address (serial monitor shows "got ip:")
- ✅ Verify you're on the same network as ESP32-C6
- ✅ Try accessing directly: `http://192.168.1.xxx/`
- ✅ Check firewall settings on your computer
- ✅ Ping the ESP32-C6 IP address:
  ```bash
  ping 192.168.1.100
  ```
- ✅ Check if web server started:
  ```
  I (6000) WEB_SERVER: HTTP server started on port 80
  ```

**Test:**
```bash
# Use curl to test API
curl http://192.168.1.100/api/data
```

---

#### 4. WebSocket Connection Fails

**[VI] Kết nối WebSocket thất bại**

**Symptoms:**
- Dashboard loads but shows "Disconnected"
- Event log shows "WebSocket error"

**Solutions:**
- ✅ Check browser console for JavaScript errors (F12)
- ✅ Verify WebSocket URL in browser console
- ✅ Check ESP32-C6 has free heap (should be >50KB)
- ✅ Reduce `WEBSOCKET_MAX_CONNECTIONS` if needed:
  ```c
  #define WEBSOCKET_MAX_CONNECTIONS 2
  ```
- ✅ Clear browser cache and reload

---

#### 5. Chart Not Updating / Frozen

**[VI] Biểu đồ không cập nhật / bị đóng băng**

**Symptoms:**
- Dashboard loads but chart doesn't update
- Metrics show "-" or stale data

**Solutions:**
- ✅ Check serial monitor for "WS metrics" logs
- ✅ Verify `ws_msg_per_sec` is ~100 in `/api/stats`
- ✅ Check browser console for JavaScript errors
- ✅ Ensure IIS3DWB is sending data (check FIFO level)
- ✅ Try different browser (Chrome, Firefox, Edge)

**Test:**
```bash
# Check WebSocket broadcast in serial monitor
idf.py monitor | grep "WS metrics"
```

---

#### 6. High CPU Usage / Slow Performance

**[VI] CPU cao / Hiệu suất chậm**

**Symptoms:**
- ESP32-C6 becomes unresponsive
- WebSocket message rate drops below 80 Hz
- High processing time (>500 us)

**Solutions:**
- ✅ Reduce chart rolling window:
  ```javascript
  const maxPoints = 100;  // Instead of 200
  ```
- ✅ Increase WebSocket broadcast period:
  ```c
  TickType_t broadcast_period = pdMS_TO_TICKS(20);  // 50 Hz
  ```
- ✅ Reduce FIFO watermark:
  ```c
  #define IIS3DWB_FIFO_WATERMARK 96
  ```
- ✅ Check for memory leaks (free heap decreasing)

---

#### 7. Data Loss / Dropped Samples

**[VI] Mất dữ liệu / Mẫu bị bỏ**

**Symptoms:**
```
W (10000) DATA_BUFFER: Dropped samples: 15
W (10000) DATA_BUFFER: Buffer overflow detected
```

**Solutions:**
- ✅ Increase buffer size:
  ```c
  #define DATA_BUFFER_SIZE 2000
  ```
- ✅ Increase IMU task priority:
  ```c
  #define IMU_TASK_PRIORITY 10
  ```
- ✅ Increase IMU task stack size:
  ```c
  #define IMU_TASK_STACK_SIZE 16384
  ```

---

#### 8. Compilation Errors

**[VI] Lỗi biên dịch**

**Common errors:**

**Error:** `esp_idf not found`
```bash
# Solution: Set up environment
. $HOME/esp/esp-idf/export.sh
```

**Error:** `Component not found`
```bash
# Solution: Update submodules
git submodule update --init --recursive
```

**Error:** `Python dependencies missing`
```bash
# Solution: Install requirements
pip install -r $IDF_PATH/requirements.txt
```

### Debug Commands

**[VI] Lệnh gỡ lỗi**

```bash
# Full build with verbose output
idf.py -v build

# Clean build (if issues persist)
idf.py fullclean
idf.py build

# Monitor with timestamps
idf.py monitor --print_filter "*"

# Check partition table
idf.py partition-table

# Check binary size
idf.py size-components

# Erase flash completely
idf.py erase-flash

# GDB debugging
idf.py openocd
idf.py gdb
```

### Getting Help

**[VI] Nhận trợ giúp**

1. **Check Serial Monitor Output**: Most issues show up in logs
2. **Enable Verbose Logging**: Set `ESP_LOG_VERBOSE` for all components
3. **Check API Stats**: Visit `/api/stats` for system health
4. **Browser Console**: Check F12 console for JavaScript errors
5. **GitHub Issues**: [Report bugs](https://github.com/hbqtechnologycompany/ESP32-C6-Multi-Sensor-IMU-Module/issues)
6. **Contact Support**: contact@hbqsolution.com

## � Data Analysis

**[VI] Phân tích dữ liệu**

### Real-time Visualization

**[VI] Trực quan hóa thời gian thực**

The embedded web dashboard provides comprehensive real-time analysis:

#### Time-Series Charts
- **Live Plotting**: Three-axis acceleration display (X, Y, Z)
- **Rolling Window**: 200-point buffer for smooth scrolling
- **Auto-Scaling**: Y-axis automatically adjusts to data range
- **Color Coding**:
  - 🔴 **X-axis**: Red (#ef4444)
  - 🟢 **Y-axis**: Green (#10b981)
  - 🔵 **Z-axis**: Blue (#3b82f6)

#### Statistical Metrics
- **Magnitude Calculation**: Real-time |g| = √(x² + y² + z²)
- **Sampling Rate**: Actual sensor ODR vs configured ODR
- **Throughput Monitoring**: Message/s and sample/s counters
- **FIFO Health**: Real-time FIFO level and batch size

### Data Export

**[VI] Xuất dữ liệu**

#### CSV Format

**Download:**
```http
GET /api/download?format=csv
```

**Example Output:**
```csv
timestamp_us,x_g,y_g,z_g,magnitude_g
1234567890,0.012,-0.023,0.985,0.986
1234567928,0.013,-0.024,0.986,0.987
1234567966,0.011,-0.022,0.984,0.985
...
```

**Use Cases:**
- ✅ **Excel**: Direct import for charts and analysis
- ✅ **MATLAB**: `data = readmatrix('imu_data.csv')`
- ✅ **Python Pandas**: `df = pd.read_csv('imu_data.csv')`
- ✅ **R**: `data <- read.csv('imu_data.csv')`

#### JSON Format

**Download:**
```http
GET /api/download?format=json
```

**Example Output:**
```json
{
  "samples": [
    {
      "timestamp_us": 1234567890,
      "x_g": 0.012,
      "y_g": -0.023,
      "z_g": 0.985,
      "magnitude_g": 0.986
    },
    ...
  ],
  "metadata": {
    "count": 100,
    "odr_hz": 26700,
    "export_time": 1234567890
  }
}
```

**Use Cases:**
- ✅ **Web Applications**: Direct JSON parsing
- ✅ **Python**: `json.load()` for analysis
- ✅ **JavaScript**: Fetch API integration
- ✅ **Node.js**: Backend processing

#### Real-time WebSocket Streaming

**JavaScript Example:**
```javascript
const ws = new WebSocket('ws://192.168.1.100/ws/data');
let allData = [];

ws.onmessage = (event) => {
  const payload = JSON.parse(event.data);
  const { chunks } = payload;
  
  // Accumulate data
  for (let i = 0; i < chunks.x.length; i++) {
    allData.push({
      x: chunks.x[i],
      y: chunks.y[i],
      z: chunks.z[i],
      mag: Math.sqrt(chunks.x[i]**2 + chunks.y[i]**2 + chunks.z[i]**2)
    });
  }
  
  // Export when needed
  if (allData.length > 1000) {
    downloadAsCSV(allData);
    allData = [];
  }
};
```

**Python Example:**
```python
import websocket
import json
import csv

data = []

def on_message(ws, message):
    payload = json.loads(message)
    chunks = payload['chunks']
    
    for i in range(len(chunks['x'])):
        data.append({
            'x': chunks['x'][i],
            'y': chunks['y'][i],
            'z': chunks['z'][i],
            'mag': payload['mag']
        })
    
    # Save every 1000 samples
    if len(data) > 1000:
        save_to_csv(data)
        data.clear()

ws = websocket.WebSocketApp(
    "ws://192.168.1.100/ws/data",
    on_message=on_message
)
ws.run_forever()
```

### Analysis Examples

**[VI] Ví dụ phân tích**

#### 1. Vibration Analysis (Python)
```python
import pandas as pd
import numpy as np
from scipy import signal

# Load data
df = pd.read_csv('imu_data.csv')

# Calculate FFT
fft = np.fft.fft(df['magnitude_g'])
freq = np.fft.fftfreq(len(fft), 1/26700)  # 26.7 kHz sampling

# Find dominant frequency
dominant_freq = freq[np.argmax(np.abs(fft[1:])) + 1]
print(f"Dominant frequency: {dominant_freq:.2f} Hz")
```

#### 2. Impact Detection (JavaScript)
```javascript
// Detect impacts above threshold
const threshold = 2.0;  // 2g threshold
let impacts = [];

allData.forEach((sample, idx) => {
  if (sample.mag > threshold) {
    impacts.push({
      index: idx,
      time: sample.timestamp_us / 1e6,  // seconds
      magnitude: sample.mag
    });
  }
});

console.log(`Detected ${impacts.length} impacts`);
```

#### 3. Statistical Analysis (MATLAB)
```matlab
% Load CSV data
data = readmatrix('imu_data.csv');
x = data(:, 2);
y = data(:, 3);
z = data(:, 4);
mag = data(:, 5);

% Calculate statistics
fprintf('Mean magnitude: %.4f g\n', mean(mag));
fprintf('Std deviation: %.4f g\n', std(mag));
fprintf('Peak magnitude: %.4f g\n', max(mag));

% Plot histogram
histogram(mag, 50);
xlabel('Magnitude (g)');
ylabel('Count');
title('Acceleration Magnitude Distribution');
```

## 🔮 Future Enhancements

**[VI] Cải tiến trong tương lai**

### Planned Features

**[VI] Tính năng dự kiến**

#### 🤖 Machine Learning
- [ ] **Anomaly Detection**: Automatic detection of unusual vibration patterns
- [ ] **Pattern Recognition**: Classify different motion types
- [ ] **Predictive Maintenance**: Detect early signs of mechanical failure
- [ ] **Edge AI**: TensorFlow Lite on ESP32-C6

#### ☁️ Cloud Integration
- [ ] **AWS IoT Core**: MQTT publishing to AWS
- [ ] **Azure IoT Hub**: Bi-directional communication
- [ ] **Google Cloud IoT**: Real-time analytics
- [ ] **ThingsBoard**: Dashboard and alerts
- [ ] **InfluxDB**: Time-series database storage

#### 📱 Mobile App
- [ ] **React Native**: Cross-platform mobile app
- [ ] **Flutter**: Native iOS/Android performance
- [ ] **Push Notifications**: Alert on events
- [ ] **Offline Mode**: Cache data when disconnected

#### 📊 Advanced Analytics
- [ ] **FFT Visualization**: Frequency domain analysis
- [ ] **Spectrogram**: Time-frequency heatmap
- [ ] **Statistical Reports**: Automated PDF generation
- [ ] **Data Comparison**: Compare multiple sessions
- [ ] **Event Markers**: Annotate significant events

#### 🌐 Multi-Device Support
- [ ] **Sensor Synchronization**: Multiple ESP32-C6 devices
- [ ] **Timestamp Alignment**: NTP time synchronization
- [ ] **Distributed Dashboard**: Monitor all devices
- [ ] **Load Balancing**: Distribute WebSocket connections

### Performance Improvements

**[VI] Cải tiến hiệu năng**

#### ⚡ Edge Computing
- [ ] **On-Device FFT**: Real-time frequency analysis
- [ ] **Kalman Filtering**: Noise reduction
- [ ] **Data Compression**: Reduce bandwidth usage
- [ ] **Smart Decimation**: Adaptive sampling rate

#### 💾 Storage & Caching
- [ ] **SD Card Logging**: Store data to SD card
- [ ] **Flash Storage**: Use ESP32-C6 flash partition
- [ ] **Circular File Buffer**: Continuous recording
- [ ] **Intelligent Caching**: Cache frequently accessed data

#### 🚀 Optimization
- [ ] **ESP32-C6 WiFi 6**: Take advantage of WiFi 6 features
- [ ] **HTTP/2 Server**: Multiplexed connections
- [ ] **WebAssembly**: Client-side processing
- [ ] **Service Workers**: Offline PWA capabilities

#### 🔒 Security
- [ ] **HTTPS**: TLS/SSL encryption
- [ ] **Authentication**: User login system
- [ ] **API Keys**: Secure API access
- [ ] **OTA Updates**: Over-the-air firmware updates

## 💰 Support & Purchase

**[VI] Hỗ trợ & Mua hàng**

### 🛒 Buy Hardware

**ESP32-C6 Development Kit with IIS3DWB Sensor**

Visit our official store to purchase complete development kits, sensors, and accessories:

**🏬 HBQ Technology Store**  
🌐 Website: [https://store.hbqsolution.com/](https://store.hbqsolution.com/)

**Available Products:**
- ✅ ESP32-C6 Development Board
- ✅ IIS3DWB High-Speed Accelerometer Module
- ✅ Complete Sensor Kit with cables and documentation
- ✅ CM5 Gateway for industrial applications

### 📞 Contact Information

**[VI] Thông tin liên hệ**

**HBQ Technology Company**

📧 **Email:**
- General: contact@hbqsolution.com
- Support: hbqsolution@gmail.com

📱 **Phone:**
- (+84) 035 719 1643
- (+84) 094 850 7979

🏢 **Address:**
- 31, Đường số 8, Cityland Garden Hill
- Phường An Nhơn, Quận Gò Vấp
- Thành phố Hồ Chí Minh, Việt Nam

🌐 **Website:** [https://hbqsolution.com/](https://hbqsolution.com/)

### 💝 Support the Project

**[VI] Ủng hộ dự án**

If you find this project useful, consider supporting our development:

**💳 PayPal Donation:**  
[![Donate](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://paypal.me/hbqtechnology)

**⭐ GitHub Sponsors:**  
[![Sponsor](https://img.shields.io/badge/Sponsor-GitHub-ea4aaa.svg)](https://github.com/sponsors/hbqtechnologycompany)

**🌟 Star the Repository:**
```bash
# Give us a star on GitHub!
https://github.com/hbqtechnologycompany/ESP32-C6-Multi-Sensor-IMU-Module
```

Your support helps us:
- 🔧 Develop new features
- 📚 Create better documentation
- 🐛 Fix bugs faster
- 🆓 Keep the project open-source

### 🤝 Business Inquiries

**[VI] Hợp tác kinh doanh**

For custom development, OEM partnerships, or bulk orders:

📧 **Contact:** contact@hbqsolution.com  
📄 **Subject:** "Business Inquiry - ESP32-C6 Project"

We offer:
- ✅ Custom firmware development
- ✅ Hardware customization
- ✅ Technical consulting
- ✅ Bulk pricing for resellers
- ✅ White-label solutions

## 📚 Documentation

**[VI] Tài liệu**

### Project Documentation
- 📖 **[Main Project README](../README.md)** - Overview of all modules
- 📘 **[Detailed Guide](../DETAILED_GUIDE.md)** - In-depth technical guide
- 📙 **[Hardware Guide](../docs/HARDWARE.md)** - Hardware connections and setup
- 📗 **[API Documentation](../docs/API.md)** - Complete API reference

### External Resources
- 🔗 **[ESP32-C6 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)**
- 🔗 **[IIS3DWB Datasheet](https://www.st.com/resource/en/datasheet/iis3dwb.pdf)**
- 🔗 **[ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)**
- 🔗 **[Chart.js Documentation](https://www.chartjs.org/docs/latest/)**

### Video Tutorials
- 🎥 **Coming Soon**: Setup and configuration guide
- 🎥 **Coming Soon**: Advanced features walkthrough
- 🎥 **Coming Soon**: Data analysis examples

## 🤝 Contributing

**[VI] Đóng góp**

We welcome contributions from the community! Here's how you can help:

### How to Contribute

1. **🍴 Fork the Repository**
   ```bash
   git clone https://github.com/YOUR_USERNAME/ESP32-C6-Multi-Sensor-IMU-Module.git
   ```

2. **🌿 Create a Feature Branch**
   ```bash
   git checkout -b feature/amazing-feature
   ```

3. **✨ Make Your Changes**
   - Follow existing code style
   - Add comments where necessary
   - Update documentation

4. **✅ Test Your Changes**
   ```bash
   idf.py build
   idf.py flash monitor
   # Test thoroughly before submitting
   ```

5. **📝 Commit with Clear Messages**
   ```bash
   git commit -m "Add: Amazing new feature"
   git commit -m "Fix: Bug in WebSocket handling"
   git commit -m "Docs: Update README with examples"
   ```

6. **🚀 Push and Create Pull Request**
   ```bash
   git push origin feature/amazing-feature
   # Then create PR on GitHub
   ```

### Contribution Guidelines

- ✅ **Code Quality**: Follow C/C++ best practices
- ✅ **Documentation**: Update README and comments
- ✅ **Testing**: Test on real hardware
- ✅ **Commits**: Use clear, descriptive commit messages
- ✅ **Issues**: Reference issue numbers in commits

### Areas We Need Help

- 🐛 **Bug Fixes**: Report or fix bugs
- 📚 **Documentation**: Improve docs and examples
- 🌐 **Translations**: Translate to other languages
- ✨ **Features**: Add new capabilities
- 🧪 **Testing**: Test on different hardware configurations

## 📄 License

**[VI] Giấy phép**

This project is licensed under the **MIT License**.

```
MIT License

Copyright (c) 2024 HBQ Technology Company

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

See the **[LICENSE](../LICENSE)** file for full details.

### Third-Party Licenses

This project uses the following open-source libraries:

- **ESP-IDF**: Apache 2.0 License
- **Chart.js**: MIT License
- **FreeRTOS**: MIT License

## 🌟 Acknowledgments

**[VI] Lời cảm ơn**

Special thanks to:

- 🙏 **Espressif Systems** - For the amazing ESP32-C6 platform and ESP-IDF
- 🙏 **STMicroelectronics** - For the IIS3DWB high-speed accelerometer
- 🙏 **Chart.js Team** - For the excellent charting library
- 🙏 **Open Source Community** - For inspiration and contributions
- 🙏 **Our Customers** - For valuable feedback and support

---

## 📞 Support

**[VI] Hỗ trợ**

### Need Help?

- 📖 **Documentation**: Check this README and linked docs
- 💬 **GitHub Issues**: [Report bugs or request features](https://github.com/hbqtechnologycompany/ESP32-C6-Multi-Sensor-IMU-Module/issues)
- 📧 **Email Support**: contact@hbqsolution.com
- 💬 **Community**: Join discussions on GitHub

### Before Asking for Help

Please provide:
1. ✅ ESP-IDF version (`idf.py --version`)
2. ✅ ESP32-C6 module type
3. ✅ Complete error message or log output
4. ✅ Steps to reproduce the issue
5. ✅ What you've already tried

---

<div align="center">

**⭐ Built with ❤️ for high-performance IMU monitoring ⭐**

**ESP32-C6 | IIS3DWB | Real-time WebSocket | Modern Web Dashboard**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.4-blue.svg)](https://github.com/espressif/esp-idf)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](https://github.com/hbqtechnologycompany/ESP32-C6-Multi-Sensor-IMU-Module/pulls)

**[⬆ Back to Top](#esp32-c6-iis3dwb-high-speed-web-monitor)**

</div>
