# SynaptaNode — Next Session Briefing

## สถานะปัจจุบัน (branch: main)

Library ยังเป็น Arduino framework อยู่ — compile ไม่ผ่าน เพราะ `libmqtt.a` ใน Arduino ESP32 SDK ถูก pre-compile ด้วย `CONFIG_MQTT_PROTOCOL_311=y` ทำให้ใช้ MQTT5 API ไม่ได้เลย

## การตัดสินใจ

**เปลี่ยนจาก `framework = arduino` → `framework = espidf`**

เหตุผล:
- ESP-IDF mode compile `libmqtt.a` จาก source → ใส่ `CONFIG_MQTT_PROTOCOL_5=y` ใน `sdkconfig.defaults` ได้
- MQTT5 จำเป็นจริงสำหรับระบบนี้ (manifest expiry 3600s → กัน zombie auto-discovery เมื่อ node ตาย)
- ไม่มี Arduino-compatible MQTT5 library ที่ใช้ได้จริง (ตรวจสอบหมดแล้ว: 256dpi, bertmelis, ESP32MQTTClient ทั้งหมด MQTT3 เท่านั้น)

User-facing API ต้องง่ายเหมือนเดิม เปลี่ยนแค่ `setup()/loop()` → `app_main()` (user รับได้)

---

## งานที่ต้องทำทั้งหมด

### 1. ไฟล์ core library (ทำก่อน)

#### `src/NodeConfig.h`
- ลบ `#include <Arduino.h>`
- `String` → `std::string`
- `load()/save()` ยังต้องมี (ใช้ NVS API แทน Preferences)

#### `src/SynaptaRegistry.h`
- **ไม่ต้องเปลี่ยน** (ใช้แต่ `std::vector` อยู่แล้ว)

#### `src/SynaptaDevice.h`
- ลบ `#include <Arduino.h>`
- `String` → `std::string`
- ลบ `#if defined(ESP_ARDUINO_VERSION_MAJOR)` conditional → ใช้แค่ `int8_t _pwmChannel = -1` เสมอ
- `bool _btnLastReading = HIGH;` → `bool _btnLastReading = true;`
- เพิ่ม `#include "esp_timer.h"` สำหรับ millis()

#### `src/SynaptaDevice.cpp`
- ลบ `#include <Arduino.h>`, `#include <Preferences.h>`, `#include <math.h>`
- เพิ่ม ESP-IDF headers:
  ```cpp
  #include "driver/gpio.h"
  #include "driver/ledc.h"
  #include "nvs.h"
  #include "nvs_flash.h"
  #include "esp_timer.h"
  ```
- `millis()` → helper function:
  ```cpp
  static inline uint32_t _millis() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
  ```
- `attachPin()`: `pinMode/digitalWrite` → `gpio_config_t + gpio_set_level()`
- `attachPWM()`: `ledcAttach/ledcWrite` → `ledc_timer_config + ledc_channel_config`
  - ใช้ static counter สำหรับ channel (เหมือน Arduino v2 แต่ ESP-IDF style)
- `attachButton()`: `INPUT_PULLUP` → `gpio_config_t` with `GPIO_PULLUP_ENABLE`
- `_writePWM()`: `ledcWrite` → `ledc_set_duty + ledc_update_duty`
- `_executeDigital()`: `HIGH/LOW` → `1/0`
- `_loop()` button: `digitalRead(_btnPin) == LOW` → `gpio_get_level((gpio_num_t)_btnPin) == 0`
- `_loop()` timing: `millis()` → `_millis()`
- `_parseBool()`: `String.equalsIgnoreCase` → `strcasecmp()`
- `_handleConfig()`: `String.indexOf/substring` → `std::string::find/substr + std::stoi`
- `_publishState()`: `String(payload)` → `std::to_string() / snprintf`
- NVS (แทน Preferences):
  ```cpp
  // read
  nvs_handle_t h;
  nvs_open("syn-pins", NVS_READONLY, &h);
  int32_t v = -1; nvs_get_i32(h, key, &v);
  nvs_close(h);
  // write
  nvs_open("syn-pins", NVS_READWRITE, &h);
  nvs_set_i32(h, key, val); nvs_commit(h); nvs_close(h);
  ```
- `_manifestEntry()`, `_cmdTopic()`, `_stateTopic()`, `_configTopic()`, `_nvKey()`:
  - `String` → `std::string` (logic เหมือนเดิมทุกอย่าง)

#### `src/SynaptaNode.h`
- ลบ `#include <Arduino.h>`, `#include <WiFi.h>`
- เปลี่ยน include:
  ```cpp
  #include "mqtt_client.h"   // ← ใน ESP-IDF mode ใช้แบบนี้ได้เลย (ไม่ต้อง full path)
  ```
- `String` → `std::string` ใน `_SynaptaMsg` struct
- ลบ `void loop()` ออกจาก public API
- เพิ่ม `TaskHandle_t _loopTask = nullptr;` ใน private

#### `src/SynaptaNode.cpp`
- ลบ WiFi Arduino, ใช้ ESP-IDF:
  ```cpp
  #include "esp_wifi.h"
  #include "esp_event.h"
  #include "esp_netif.h"
  #include "nvs_flash.h"
  #include "esp_log.h"
  ```
- WiFi init pattern (ใช้ EventGroup แทน polling loop):
  ```cpp
  static EventGroupHandle_t s_wifiEG;
  #define WIFI_CONNECTED_BIT BIT0
  
  // handler:
  if (base==WIFI_EVENT && id==WIFI_EVENT_STA_START)    esp_wifi_connect();
  if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
  if (base==IP_EVENT   && id==IP_EVENT_STA_GOT_IP)
      xEventGroupSetBits(s_wifiEG, WIFI_CONNECTED_BIT);
  
  // block จนเชื่อม:
  xEventGroupWaitBits(s_wifiEG, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
  ```
- NVS flash init ใน `_init()` ก่อนอื่น:
  ```cpp
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase(); nvs_flash_init();
  }
  ```
- `start()` สร้าง background FreeRTOS task แทนที่ user ต้องเรียก `loop()`:
  ```cpp
  xTaskCreate([](void* arg) {
      auto* self = static_cast<SynaptaNodeClass*>(arg);
      while (true) {
          std::vector<_SynaptaMsg> inbox;
          xSemaphoreTake(self->_mutex, portMAX_DELAY);
          inbox.swap(self->_inbox);
          xSemaphoreGive(self->_mutex);
          for (auto& msg : inbox) self->_dispatch(msg);
          for (auto* d : self->_devices) d->_loop();
          vTaskDelay(pdMS_TO_TICKS(10));
      }
  }, "synapta_main", 8192, this, 5, &_loopTask);
  ```
- MAC address: `WiFi.macAddress()` → `esp_wifi_get_mac(WIFI_IF_STA, mac)`
- `Serial.printf` → `printf` หรือ `ESP_LOGI(TAG, ...)`
- `String` → `std::string` ทุกที่ (`.isEmpty()` → `.empty()`, `+` operator ยังใช้ได้)
- `_dispatch()`: `msg.responseTopic.isEmpty()` → `msg.responseTopic.empty()`
- MQTT5 include ใน ESP-IDF mode:
  ```cpp
  #include "mqtt_client.h"  // ← ถูกต้องสำหรับ ESP-IDF framework
  ```
  **หมายเหตุ:** `esp_mqtt5_publish_property_config_t` และ `esp_mqtt5_client_set_publish_property` ยังใช้ได้เหมือนเดิมเมื่อ `CONFIG_MQTT_PROTOCOL_5=y`

- NodeConfig `load()/save()` → implement ด้วย NVS ใน NodeConfig.cpp แยกต่างหาก หรือ inline ใน SynaptaNode.cpp

#### `src/Synapta.h`
- แก้ comment ที่พูดถึง `loop()` ออก
- ไม่ต้องเปลี่ยนอย่างอื่น

#### `src/SynaptaDevices.h`
- เปลี่ยน comment ที่พูดถึง `setup()/loop()` → `app_main()`
- ไม่ต้องเปลี่ยน logic

---

### 2. ไฟล์ build system (ใหม่ทั้งหมด)

#### `CMakeLists.txt` (root ของ library)
```cmake
cmake_minimum_required(VERSION 3.16)
idf_component_register(
    SRCS
        "src/SynaptaNode.cpp"
        "src/SynaptaDevice.cpp"
    INCLUDE_DIRS "src"
    REQUIRES
        mqtt
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        driver
        esp_timer
        freertos
        log
)
```

#### `sdkconfig.defaults` (root ของ library — สำหรับ test build)
```
CONFIG_MQTT_PROTOCOL_5=y
CONFIG_MQTT_TRANSPORT_SSL=y
```

**หมายเหตุ:** user ต้องสร้างไฟล์นี้เองในโปรเจคของตัวเองด้วย (เขียนใน README)

---

### 3. ไฟล์ metadata

#### `library.json`
```json
{
  "name": "SynaptaNode",
  "version": "3.0.0",
  "description": "ESP32 node library for Synapta smart home — true MQTT 5, auto-discovery, NVS pin config",
  "keywords": ["mqtt", "mqtt5", "esp32", "smarthome", "iot", "synapta"],
  "authors": [{ "name": "Synapta", "maintainer": true }],
  "repository": {
    "type": "git",
    "url": "https://github.com/Ninlapat5G/SynaptaNode-V0"
  },
  "license": "LGPL-2.1",
  "frameworks": ["espidf"],
  "platforms": ["espressif32"]
}
```

#### `library.properties`
```
name=SynaptaNode
version=3.0.0
author=Synapta
maintainer=Synapta
sentence=ESP32 smart home node library with true MQTT 5 support
paragraph=Uses ESP-IDF framework. Supports MQTT 5 features: messageExpiryInterval, responseTopic, correlationData, Will Message.
category=Communication
url=https://github.com/Ninlapat5G/SynaptaNode-V0
architectures=esp32
depends=
```

---

### 4. Examples (ทุก example ใน `examples/0x_*/`)

แต่ละ example ต้องเปลี่ยน:

**`platformio.ini`** — เปลี่ยน `framework = arduino` → `framework = espidf`

**`src/main.cpp`** — pattern ใหม่:
```cpp
#include <Synapta.h>

SynaptaDigital lamp("bedroom/lamp");

extern "C" void app_main() {
    Synapta.wifi("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
    Synapta.baseTopic("Mylab/smarthome");
    Synapta.start();
    // ไม่ต้อง loop() — library จัดการเองใน background task
}
```

**`sdkconfig.defaults`** (ใหม่ ใส่ใน root ของแต่ละ example):
```
CONFIG_MQTT_PROTOCOL_5=y
CONFIG_MQTT_TRANSPORT_SSL=y
```

ข้อสังเกตสำหรับแต่ละ example:
- ลบ `Serial.begin(115200)` ออก
- `Serial.printf(...)` → `printf(...)`
- Example 03, 06 (DHT sensor): เปลี่ยนเป็น mock sensor (return ค่าคงที่) เพราะ DHT Arduino library ใช้ใน ESP-IDF ไม่ได้ตรงๆ

---

### 5. README.md

เขียนใหม่ทั้งหมด ครอบคลุม:
- ESP-IDF framework — ทำไมต้องเปลี่ยน (MQTT5 ใน Arduino SDK compile มา MQTT3)
- Installation: PlatformIO + `framework = espidf`
- ต้องสร้าง `sdkconfig.defaults` ในโปรเจค
- Quick Start ด้วย `app_main()` (ไม่มี loop)
- API เหมือนเดิมทุกอย่าง ยกเว้น loop() ไม่ต้องเรียก
- Broker requirements เหมือนเดิม

---

### 6. หลังเสร็จ

```bash
cd SynaptaNode
git add -A
git commit -m "feat: migrate to ESP-IDF framework for true MQTT5 support"
git tag -a "v3.0.0-espidf" -m "ESP-IDF migration: Arduino IDE → PlatformIO ESP-IDF"
git push origin main --tags
```

---

## สิ่งที่ต้องคุยหลังจากนี้ (session หน้าถัดไป)

หลัง push เสร็จ ต้องคุยเรื่อง **2-version strategy**:
- **v3.x** (branch `main`): ESP-IDF + MQTT5 — สำหรับ PlatformIO
- **v2.x** (branch `arduino`): Arduino + MQTT3 — สำหรับคน Arduino IDE

ประเด็นที่ต้องตัดสินใจ:
1. จะแยก branch หรือแยก repo?
2. Arduino version จะ drop feature ไหนบ้าง? (manifest expiry ไม่มี, responseTopic workaround via JSON payload?)
3. Library name แยกหรือใช้ชื่อเดียวกัน?

---

## หมายเหตุสำคัญ

- `NodeConfig.h` ถูก revert กลับเป็น Arduino version แล้ว (session นี้เขียนไปแล้วแต่ roll back)
- ไฟล์อื่นทั้งหมดยังเป็น Arduino version อยู่ — ยังไม่ได้เปลี่ยน
- `SynaptaNode.h` มี include path `"mqtt/esp-mqtt/include/esp_mqtt_client.h"` — **ต้องเปลี่ยนเป็น `"mqtt_client.h"`** ใน ESP-IDF mode
