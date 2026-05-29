# SynaptaNode

ESP32 library สำหรับระบบ [SynaptaOS](https://github.com/Ninlapat5G/SynaptaOS-V0) — เชื่อม ESP32 เข้ากับ AI smart home ผ่าน **MQTT 5** ใน 10 บรรทัด

> **v3.0 — ESP-IDF framework**  
> เปลี่ยนจาก Arduino → ESP-IDF เพื่อรองรับ MQTT 5 จริง  
> Arduino SDK compile `libmqtt.a` มาแบบ MQTT 3 เท่านั้น — ใช้ MQTT 5 API ไม่ได้

---

## How it fits

```
Web App (browser)
    │  publish /{topic}/set
    ▼
MQTT Broker (MQTT 5)
    │  subscribe /{topic}/set  ←── ESP32 + SynaptaNode
    │  publish /{topic}/state (retain, expiry 5 min)
    │  publish /nodes/{id}/manifest (auto-discovery)
    │  publish /nodes/{id}/status (online/offline will)
    ▼
Web App — UI sync, auto-discover devices
```

---

## Installation (PlatformIO)

**ต้องใช้ PlatformIO + ESP-IDF framework** — ไม่รองรับ Arduino IDE

**ขั้นตอน**

1. สร้าง PlatformIO project แล้วใส่ใน `platformio.ini`:

```ini
[env:esp32dev]
platform  = espressif32
board     = esp32dev
framework = espidf
lib_deps  =
    https://github.com/Ninlapat5G/SynaptaNode-V0.git
```

2. สร้างไฟล์ `sdkconfig.defaults` ในโปรเจค (สำคัญมาก — ขาดไม่ได้):

```
CONFIG_MQTT_PROTOCOL_5=y
CONFIG_MQTT_TRANSPORT_SSL=y
```

3. `#include <Synapta.h>` ใน `src/main.cpp`

---

## Quick Start

```cpp
#include <Synapta.h>

SynaptaDigital lamp("bedroom/lamp");

extern "C" void app_main() {
    Synapta.wifi("MyWiFi", "password");
    Synapta.baseTopic("home/smarthome");
    Synapta.start();
    // ไม่ต้อง loop() — library รัน background task ให้เอง
}
```

เมื่อ node เชื่อม MQTT สำเร็จ → publish manifest ให้ Web App ค้นพบ devices อัตโนมัติ

---

## API

### Synapta (singleton)

| Method | Description |
|--------|-------------|
| `wifi(ssid, pass)` | ตั้ง WiFi credentials |
| `baseTopic(base)` | ตั้ง base topic (ต้องตรงกับ Web App) |
| `broker(host, port, tls)` | เปลี่ยน broker (default: HiveMQ public, TLS 8883) |
| `mqttAuth(user, pass)` | สำหรับ broker ที่ต้อง auth |
| `nodeId(id)` | ตั้งชื่อ node เอง (default: derive จาก MAC address) |
| `start()` | เชื่อม WiFi (blocking) แล้วเริ่ม MQTT + background task |
| `configure(ssid, pass, base)` | บันทึก credential ลง NVS + start |
| `begin()` | โหลด credential จาก NVS แล้ว start |
| `isConnected()` | `true` เมื่อ MQTT พร้อม |
| `onConnect(cb)` / `onDisconnect(cb)` | event callbacks |

### Device types

```cpp
SynaptaDigital relay ("bedroom/relay");   // ON/OFF
SynaptaAnalog  dimmer("bedroom/dimmer");  // 0–255
SynaptaSensor  temp  ("bedroom/temp");    // publish only
```

| Method | ใช้กับ | Description |
|--------|--------|-------------|
| `onCommand(cb)` | Digital | `cb(bool on)` |
| `onValue(cb)` | Analog | `cb(int value)` |
| `attachPin(pin)` | Digital | auto drive GPIO HIGH/LOW |
| `attachPWM(pin)` | Analog | auto drive PWM ผ่าน LEDC |
| `attachButton(pin)` | Digital | ปุ่มกด active-low, debounce 50ms, toggle + publish |
| `every(ms, cb)` | Sensor | publish ทุก ms |
| `turnOn()` / `turnOff()` / `toggle()` | Digital | สั่งจาก code |
| `setLevel(0..255)` | Analog | สั่งจาก code |
| `fade(ms)` | Analog | ค่อยๆ เปลี่ยนค่า (default 200ms, 0 = instant) |
| `gamma(g)` | Analog | gamma correction สำหรับ LED (default 2.2) |

### MQTT Payload

**Digital `/set`:** `ON` / `OFF` / `toggle` / `true` / `false` / `1` / `0`

**Analog `/set`:** integer string `"0"` – `"255"`

**State `/state`** (retain, expiry 5 min): `"true"/"false"` | `"0"–"255"` | float string

**Manifest `/nodes/{id}/manifest`** (retain, expiry 1 hr): JSON auto-discovery payload

---

## MQTT 5 Features

| Feature | สถานะ | รายละเอียด |
|---------|--------|-----------|
| `messageExpiryInterval` บน state | ✅ | 300 วินาที — state เก่าหายเองเมื่อ node ไม่ reconnect |
| `messageExpiryInterval` บน manifest | ✅ | 3600 วินาที |
| Will Message | ✅ | publish "offline" อัตโนมัติเมื่อ node หลุด |
| Config ACK (`responseTopic`) | ✅ | Web App ส่ง pin config → ESP32 ตอบ ACK กลับ topic ที่ขอ |
| `correlationData` | ✅ | ส่งกลับพร้อม ACK เพื่อ match request |

---

## Examples

| Sketch | สอนอะไร |
|--------|---------|
| `01_BasicDigital` | relay ON/OFF พื้นฐาน |
| `02_MultiDevice` | หลาย device + 2 callback styles |
| `03_Sensor` | publish ค่า sensor ตามช่วงเวลา (mock sensor) |
| `04_PhysicalButton` | ปุ่มกดจริง + sync กับ Web App |
| `05_PwmDimmer` | LED dimmer + fade + gamma |
| `06_Automation` | sensor → relay rule บน node เอง (mock sensor) |
| `07_NvsCredentials` | บันทึก credential ลง NVS ครั้งเดียว |
| `08_MqttAuth` | broker ที่ต้อง user/pass |
| `09_LocalBroker` | Mosquitto/EMQX ใน LAN (no TLS) |

แต่ละ example มี `platformio.ini`, `sdkconfig.defaults`, และ code ใน `src/main.cpp`

> **หมายเหตุ 03 และ 06:** ใช้ mock sensor เพราะ DHT Arduino library ไม่รองรับ ESP-IDF  
> ถ้าต้องการ sensor จริง ให้ใช้ ESP-IDF RMT/GPIO driver โดยตรง

---

## Broker Requirements

| Broker | MQTT 5 | หมายเหตุ |
|--------|--------|----------|
| HiveMQ Public | ✅ | default ใน library (TLS 8883) |
| HiveMQ Cloud | ✅ | ใช้ `mqttAuth()` |
| EMQX | ✅ | รองรับทุก version |
| Mosquitto | ✅ | ต้องใช้ version 2.0 ขึ้นไป |

---

## License

LGPL v2.1 — ใช้ใน sketch ได้โดยไม่ต้อง open source sketch ของคุณ
