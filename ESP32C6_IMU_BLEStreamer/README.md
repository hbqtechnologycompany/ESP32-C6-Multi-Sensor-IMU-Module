
# ESP32-C6 IMU BLE Streamer

> [VI] Bộ phát BLE 5 (2M PHY + DLE) cho dữ liệu cảm biến thực (IIS2MDC, IIS3DWB, ICM45686, SCL3300) theo gói BLE tối ưu. Tối ưu cho băng thông BLE và mức tiêu thụ pin. Các mục song ngữ có nhãn [VI].

A BLE 5 (2M PHY + DLE) GATT server that streams real sensor data (IIS2MDC, IIS3DWB, ICM45686, SCL3300) in compact notifications. All sensor drivers are integrated, and configuration is flexible for bandwidth and power optimization.

## Features

[VI] Tính năng
- BLE 5, 2M PHY, Data Length Extension, MTU 247
- Một đặc tả GATT (Notify) cho streaming tốc độ cao
- Có thể bật/tắt từng cảm biến: IIS2MDC (mag), IIS3DWB (accel), ICM45686 (accel+gyro), SCL3300 (inclinometer)
- Cấu hình ODR, interval, enable/disable từng sensor qua `imu_ble_config_t`

## Directory

[VI] Cấu trúc thư mục
```
ESP32C6_IMU_BLEStreamer/
├── CMakeLists.txt
└── main/
  ├── CMakeLists.txt
  ├── ble_stream.c/.h        # BLE GATT service (notify)
  ├── imu_ble.c/.h           # Sensor aggregator/packer, cấu hình cảm biến
  ├── imu_manager.c/.h       # Quản lý sensor, đọc dữ liệu thực
  ├── sensors/               # Driver từng cảm biến (IIS2MDC, IIS3DWB, ICM45686, SCL3300)
  └── main.c                 # App entry, cấu hình hệ thống
```

## Build & Flash

[VI] Build & Flash
```
idf.py set-target esp32c6
cd ESP32C6_IMU_BLEStreamer
idf.py build flash monitor
```

## Configuration (main.c)

[VI] Cấu hình (main.c)
`imu_ble_config_t cfg` cho phép bật/tắt từng cảm biến và điều chỉnh thông số:
```
imu_ble_config_t cfg = {
  .enable_iis2mdc = true,      // Bật cảm biến từ trường IIS2MDC
  .enable_iis3dwb = true,      // Bật accelerometer IIS3DWB (có downsample cho BLE)
  .enable_icm45686 = true,     // Bật IMU 6 trục ICM45686 (accel+gyro)
  .enable_scl3300 = true,      // Bật inclinometer SCL3300
  .iis3dwb_odr_hz = 800,       // ODR cho IIS3DWB (Hz), giảm từ 26.7kHz
  .icm45686_odr_hz = 400,      // ODR cho ICM45686 (Hz)
  .packet_interval_ms = 20     // Khoảng thời gian gửi gói BLE (ms), ví dụ 20ms ~ 50Hz
};
```
Điều chỉnh các trường này để phù hợp với nhu cầu băng thông và tiết kiệm năng lượng. Tổng throughput khuyến nghị < 200–300 kbps để đảm bảo ổn định BLE.

## GATT Layout

[VI] Cấu trúc GATT
- Service UUID: `0x1815` (placeholder; use your custom UUID in production)
- Characteristic UUID: `0x2A58` (placeholder; Notify only)
- CCCD supported (enable notifications from client)
- Device Name: `IMU-BLE`

¥
## Packet Format (binary, little-endian)

[VI] Định dạng gói BLE (nhị phân, little‑endian)
Mỗi gói BLE gồm:
```
struct ble_frame_header_t {
    uint16_t frame_len;      // Tổng độ dài gói
    uint8_t  version;        // Version frame
    uint8_t  flags;          // Reserved
    uint16_t sensor_mask;    // Mask cảm biến có mặt trong gói
    uint32_t timestamp_us;   // Timestamp (us, 32 bit)
    uint32_t sequence;       // Số thứ tự frame
};
// Tiếp theo là các trường dữ liệu cảm biến, mỗi trường có type, length, value
// Ví dụ: [type][len][data...], có thể là vec3 (x,y,z) hoặc scalar (nhiệt độ)
```
Các type phổ biến:
- 0x01: IIS3DWB accel (x,y,z)
- 0x10: ICM45686 accel (x,y,z)
- 0x11: ICM45686 gyro (x,y,z)
- 0x12: ICM45686 temp
- 0x20: IIS2MDC mag (x,y,z)
- 0x21: IIS2MDC temp
- 0x30: SCL3300 angle (x,y,z)
- 0x31: SCL3300 accel (x,y,z)
- 0x32: SCL3300 temp

Mỗi trường dữ liệu:
- [type:1][len:1][payload...]
  - vec3: 6 bytes (x:int16, y:int16, z:int16)
  - scalar: 2 bytes (int16)

Scaling mặc định:
- Accel: g * 16384 (q15, ±2g)
- Gyro: dps * 131.072 (q15, ±250dps)
- Mag: mg (int16)
- Angle: deg * 100
- Temp: °C * 100

## Client Notes

[VI] Ghi chú phía client
- Subscribe to notifications on the single characteristic.
- Expect packets of variable length up to 244B, containing N×16B frames.
- Reconstruct by reading 16-byte records in order.
- For iOS/Android/WebBLE, set MTU 247 and request 2M PHY (if supported) for best throughput.

## Power Considerations

[VI] Ghi chú năng lượng
- BLE streaming @ 100–400 Hz (6-axis) typically 1–5 mA average on ESP32-C6 (order of magnitude; depends on interval/PHY).
- Significantly lower than Wi‑Fi realtime.
- For CR‑cell operation, keep duty cycle low (burst + sleep), or consider Li‑ion/LiPo for continuous streaming.

## Real Sensor Data

[VI] Dữ liệu cảm biến thực
Code đã tích hợp driver cho tất cả cảm biến. Khi cấu hình enable cảm biến nào, dữ liệu thực sẽ được đọc từ sensor tương ứng qua các hàm trong `imu_manager.c` và đóng gói gửi BLE. Không còn mô phỏng.

Để mở rộng/thay đổi:
1. Bật/tắt cảm biến trong `imu_ble_config_t` ở `main.c`.
2. Điều chỉnh ODR, interval cho phù hợp ứng dụng.
3. Nếu cần thêm cảm biến mới, thêm driver vào `sensors/` và cập nhật `imu_manager.c`.

## Troubleshooting

[VI] Xử lý sự cố
- Không nhận được notify: kiểm tra client đã enable CCCD, log có dòng `Notifications enabled`.
- Throughput thấp: kiểm tra đã enable 2M PHY, MTU 247, DLE; có thể tăng connection interval hoặc batch nhiều sample hơn mỗi notify.
- Mất gói: giảm ODR hoặc tần suất notify; tránh các tác vụ nặng trong main loop.

## License

[VI] Giấy phép
MIT (xem file LICENSE ở thư mục gốc)
