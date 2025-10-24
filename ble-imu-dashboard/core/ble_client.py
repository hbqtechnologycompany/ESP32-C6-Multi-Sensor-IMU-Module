import asyncio
from dataclasses import dataclass
from typing import List, Optional, Callable
from bleak import BleakScanner, BleakClient, BleakError
from bleak.backends.characteristic import BleakGATTCharacteristic

@dataclass
class DeviceInfo:
    name: str
    address: str
    rssi: int

@dataclass
class CharInfo:
    service_uuid: str
    char_uuid: str
    props: List[str]

class BLEHandler:
    """Handle scanning, connecting, discovering and subscribing BLE characteristics."""

    def __init__(self, on_data: Callable[[bytes], None], preferred_services: Optional[List[str]] = None):
        self.on_data = on_data
        self.client: Optional[BleakClient] = None
        self.preferred_services = [s.lower() for s in (preferred_services or [])]
        self.notify_char: Optional[BleakGATTCharacteristic] = None

    async def scan(self, timeout: float = 4.0) -> List[DeviceInfo]:
        devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
        result = []
        for address, (device, adv_data) in devices.items():
            # Get RSSI from advertisement data
            rssi = adv_data.rssi if hasattr(adv_data, 'rssi') else 0
            name = device.name or adv_data.local_name or "(unknown)"
            result.append(DeviceInfo(name=name, address=address, rssi=rssi))
        result.sort(key=lambda x: x.rssi, reverse=True)
        return result

    async def connect(self, address: str):
        self.client = BleakClient(address)
        await self.client.connect(timeout=10.0)
        print(f"[BLE] Connected: {address}")

    async def disconnect(self):
        if self.client and self.client.is_connected:
            if self.notify_char:
                await self.client.stop_notify(self.notify_char)
            await self.client.disconnect()
        self.client = None
        self.notify_char = None
        print("[BLE] Disconnected.")

    async def discover_notify_chars(self) -> List[CharInfo]:
        if not (self.client and self.client.is_connected):
            raise BleakError("Not connected to any device.")
        chars = []
        # Use services property directly (newer bleak API)
        services = self.client.services
        for svc in services:
            for ch in svc.characteristics:
                if "notify" in ch.properties:
                    chars.append(CharInfo(svc.uuid, ch.uuid, ch.properties))
        if self.preferred_services:
            def key_fn(c):
                pref = any(p in c.service_uuid.lower() for p in self.preferred_services)
                return (0 if pref else 1, c.service_uuid, c.char_uuid)
            chars.sort(key=key_fn)
        return chars

    async def start_notify(self, uuid: str):
        if not (self.client and self.client.is_connected):
            raise BleakError("Not connected.")
        def callback(_: int, data: bytearray):
            self.on_data(bytes(data))
        ch = next(
            (c for svc in self.client.services for c in svc.characteristics if c.uuid.lower() == uuid.lower()),
            None
        )
        if not ch:
            raise BleakError(f"Characteristic {uuid} not found.")
        await self.client.start_notify(ch, callback)
        self.notify_char = ch
        print(f"[BLE] Start notify {uuid}")

    async def stop_notify(self):
        if self.client and self.notify_char:
            await self.client.stop_notify(self.notify_char)
            print("[BLE] Stop notify")
            self.notify_char = None
