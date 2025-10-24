"""
ESP32-C6 IMU BLE Frame Parser
Parses data according to ESP32_EXAMPLE.md specification
"""
import struct
from dataclasses import dataclass
from typing import Dict, Optional

@dataclass
class FrameHeader:
    """BLE frame header (14 bytes)"""
    frame_len: int       # uint16
    version: int         # uint8
    flags: int           # uint8
    sensor_mask: int     # uint16
    timestamp_us: int    # uint32
    sequence: int        # uint32

@dataclass
class SensorData:
    """Decoded sensor data - stores data from ALL sensors in frame"""
    # IIS3DWB Accelerometer
    iis3dwb_accel_x: float = 0.0
    iis3dwb_accel_y: float = 0.0
    iis3dwb_accel_z: float = 0.0
    
    # ICM45686 Accelerometer  
    icm_accel_x: float = 0.0
    icm_accel_y: float = 0.0
    icm_accel_z: float = 0.0
    
    # ICM45686 Gyroscope
    gyro_x: float = 0.0
    gyro_y: float = 0.0
    gyro_z: float = 0.0
    
    # IIS2MDC Magnetometer
    mag_x: float = 0.0
    mag_y: float = 0.0
    mag_z: float = 0.0
    
    # SCL3300 Accelerometer
    scl_accel_x: float = 0.0
    scl_accel_y: float = 0.0
    scl_accel_z: float = 0.0
    
    # SCL3300 Angle (Inclinometer)
    angle_x: float = 0.0
    angle_y: float = 0.0
    angle_z: float = 0.0
    
    # Temperatures
    icm_temperature: float = 0.0
    mag_temperature: float = 0.0
    scl_temperature: float = 0.0

# TLV Type codes
TLV_IIS3DWB_ACCEL = 0x01
TLV_ICM_ACCEL = 0x10
TLV_ICM_GYRO = 0x11
TLV_ICM_TEMP = 0x12
TLV_MAG = 0x20
TLV_MAG_TEMP = 0x21
TLV_SCL_ANGLE = 0x30
TLV_SCL_ACCEL = 0x31
TLV_SCL_TEMP = 0x32

# Scaling factors
SCALE_ACCEL = 16384.0      # int16 -> g
SCALE_GYRO = 131.072       # int16 -> dps
SCALE_MAG = 1.0            # int16 -> mG
SCALE_TEMP = 100.0         # int16 -> °C
SCALE_ANGLE = 100.0        # int16 -> degrees

class ESP32FrameParser:
    """Parse ESP32-C6 IMU BLE frames"""
    
    def __init__(self):
        self.last_sequence = -1
        self.frame_count = 0
        self.error_count = 0
        
    def parse(self, data: bytes) -> Optional[tuple[FrameHeader, SensorData]]:
        """
        Parse BLE notification data
        Returns (header, sensor_data) or None if invalid
        """
        if len(data) < 14:
            self.error_count += 1
            print(f"⚠️ Frame too short: {len(data)} bytes")
            return None
        
        try:
            # Parse header (14 bytes, little-endian)
            header_data = struct.unpack('<HBBHIi', data[0:14])
            header = FrameHeader(
                frame_len=header_data[0],
                version=header_data[1],
                flags=header_data[2],
                sensor_mask=header_data[3],
                timestamp_us=header_data[4],
                sequence=header_data[5]
            )
            
            # Validate header
            if header.version != 1:
                self.error_count += 1
                print(f"⚠️ Unknown version: {header.version}")
                return None
            
            if header.frame_len != len(data):
                self.error_count += 1
                print(f"⚠️ Length mismatch: header={header.frame_len}, actual={len(data)}")
                # Don't return None, continue parsing
            
            # Detect lost frames
            if self.last_sequence >= 0:
                expected = (self.last_sequence + 1) & 0xFFFFFFFF
                if header.sequence != expected:
                    lost = (header.sequence - expected) & 0xFFFFFFFF
                    print(f"⚠️ Lost {lost} frame(s). Expected seq={expected}, got={header.sequence}")
            
            self.last_sequence = header.sequence
            self.frame_count += 1
            
            # Parse TLV payload
            sensor_data = self._parse_tlv_payload(data[14:])
            
            return (header, sensor_data)
            
        except Exception as e:
            self.error_count += 1
            print(f"❌ Parse error: {e}")
            return None
    
    def _parse_tlv_payload(self, payload: bytes) -> SensorData:
        """Parse TLV blocks in payload"""
        data = SensorData()
        offset = 0
        
        while offset < len(payload):
            if offset + 2 > len(payload):
                break
            
            tlv_type = payload[offset]
            tlv_len = payload[offset + 1]
            offset += 2
            
            if offset + tlv_len > len(payload):
                print(f"⚠️ TLV overflow: type=0x{tlv_type:02X}, len={tlv_len}")
                break
            
            tlv_data = payload[offset:offset + tlv_len]
            offset += tlv_len
            
            # Decode based on type - PARSE RIÊNG TỪNG SENSOR
            try:
                if tlv_type == TLV_IIS3DWB_ACCEL:
                    # IIS3DWB Accelerometer (6 bytes: 3 x int16)
                    if tlv_len == 6:
                        x, y, z = struct.unpack('<hhh', tlv_data)
                        data.iis3dwb_accel_x = x / SCALE_ACCEL
                        data.iis3dwb_accel_y = y / SCALE_ACCEL
                        data.iis3dwb_accel_z = z / SCALE_ACCEL
                
                elif tlv_type == TLV_ICM_ACCEL:
                    # ICM45686 Accelerometer (6 bytes: 3 x int16)
                    if tlv_len == 6:
                        x, y, z = struct.unpack('<hhh', tlv_data)
                        data.icm_accel_x = x / SCALE_ACCEL
                        data.icm_accel_y = y / SCALE_ACCEL
                        data.icm_accel_z = z / SCALE_ACCEL
                
                elif tlv_type == TLV_ICM_GYRO:
                    # ICM45686 Gyroscope (6 bytes: 3 x int16)
                    if tlv_len == 6:
                        x, y, z = struct.unpack('<hhh', tlv_data)
                        data.gyro_x = x / SCALE_GYRO
                        data.gyro_y = y / SCALE_GYRO
                        data.gyro_z = z / SCALE_GYRO
                
                elif tlv_type == TLV_ICM_TEMP:
                    # ICM45686 Temperature (2 bytes: int16)
                    if tlv_len == 2:
                        temp_raw = struct.unpack('<h', tlv_data)[0]
                        data.icm_temperature = temp_raw / SCALE_TEMP
                
                elif tlv_type == TLV_MAG:
                    # IIS2MDC Magnetometer (6 bytes: 3 x int16)
                    if tlv_len == 6:
                        x, y, z = struct.unpack('<hhh', tlv_data)
                        data.mag_x = x / SCALE_MAG
                        data.mag_y = y / SCALE_MAG
                        data.mag_z = z / SCALE_MAG
                
                elif tlv_type == TLV_MAG_TEMP:
                    # IIS2MDC Temperature (2 bytes: int16)
                    if tlv_len == 2:
                        temp_raw = struct.unpack('<h', tlv_data)[0]
                        data.mag_temperature = temp_raw / SCALE_TEMP
                
                elif tlv_type == TLV_SCL_ANGLE:
                    # SCL3300 Inclinometer angle (6 bytes: 3 x int16)
                    if tlv_len == 6:
                        x, y, z = struct.unpack('<hhh', tlv_data)
                        data.angle_x = x / SCALE_ANGLE
                        data.angle_y = y / SCALE_ANGLE
                        data.angle_z = z / SCALE_ANGLE
                
                elif tlv_type == TLV_SCL_ACCEL:
                    # SCL3300 Accelerometer (6 bytes: 3 x int16)
                    if tlv_len == 6:
                        x, y, z = struct.unpack('<hhh', tlv_data)
                        data.scl_accel_x = x / SCALE_ACCEL
                        data.scl_accel_y = y / SCALE_ACCEL
                        data.scl_accel_z = z / SCALE_ACCEL
                
                elif tlv_type == TLV_SCL_TEMP:
                    # SCL3300 Temperature (2 bytes: int16)
                    if tlv_len == 2:
                        temp_raw = struct.unpack('<h', tlv_data)[0]
                        data.scl_temperature = temp_raw / SCALE_TEMP
                
                else:
                    print(f"⚠️ Unknown TLV type: 0x{tlv_type:02X}")
            
            except Exception as e:
                print(f"❌ TLV decode error (type=0x{tlv_type:02X}): {e}")
        
        return data
    
    def get_stats(self) -> Dict[str, int]:
        """Get parser statistics"""
        return {
            "frame_count": self.frame_count,
            "error_count": self.error_count,
            "last_sequence": self.last_sequence
        }
    
    def reset_stats(self):
        """Reset statistics"""
        self.last_sequence = -1
        self.frame_count = 0
        self.error_count = 0


def parse_frame_simple(data: bytes) -> Optional[tuple[int, float, float, float]]:
    """
    Simplified parser that returns (sequence, x, y, z) for quick testing
    Uses first available accelerometer data
    """
    if len(data) < 14:
        return None
    
    try:
        # Parse header
        _, _, _, _, _, sequence = struct.unpack('<HBBHIi', data[0:14])
        
        # Parse TLV to find first accel data
        offset = 14
        while offset < len(data) - 1:
            tlv_type = data[offset]
            tlv_len = data[offset + 1]
            offset += 2
            
            if offset + tlv_len > len(data):
                break
            
            # Check if it's accelerometer data
            if tlv_type in [TLV_IIS3DWB_ACCEL, TLV_ICM_ACCEL, TLV_SCL_ACCEL] and tlv_len == 6:
                x, y, z = struct.unpack('<hhh', data[offset:offset+6])
                x_g = x / SCALE_ACCEL
                y_g = y / SCALE_ACCEL
                z_g = z / SCALE_ACCEL
                return (sequence, x_g, y_g, z_g)
            
            offset += tlv_len
        
        return None
    
    except Exception:
        return None
