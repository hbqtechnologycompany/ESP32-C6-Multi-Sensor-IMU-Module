import struct

class PacketParser:
    """Decode BLE binary packets from ESP32 IMU stream."""

    def parse(self, data: bytes):
        # Example: 1-byte sensor ID + 3 * float32 values
        if len(data) >= 13:
            sid = data[0]
            vals = struct.unpack("<3f", data[1:13])
            return {"sensor_id": sid, "values": vals}
        return {"sensor_id": 0, "values": (0.0, 0.0, 0.0)}
