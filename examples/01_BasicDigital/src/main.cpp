/*
 * 01_BasicDigital — เปิด/ปิดอุปกรณ์ดิจิตอลตัวเดียว (relay / LED)
 *
 * Pin assignment: Web App → Edit device → ใส่ Pin → Save
 * (ไม่ต้อง hardcode pin ใน code — บันทึกลง NVS อัตโนมัติ)
 */

#include <Synapta.h>

SynaptaDigital relay("bedroom/relay");

void setup() {
    Serial.begin(115200);

    Synapta.wifi("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
    Synapta.baseTopic("Mylab/smarthome");
    Synapta.start();
}

void loop() {
    Synapta.loop();
}

/*
 * ── สั่งจาก code ──
 *   relay.turnOn();
 *   relay.turnOff();
 *   relay.toggle();
 *   if (relay.isOn()) { ... }
 */
