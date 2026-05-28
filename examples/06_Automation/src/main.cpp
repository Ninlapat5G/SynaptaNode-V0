/*
 * 06_Automation — sensor → actuator แบบ rule บน node เลย
 *
 * อ่านอุณหภูมิทุก 5 วินาที:
 *   t > 30°C  → เปิดพัดลม
 *   t < 28°C  → ปิดพัดลม
 * (hysteresis 2°C กันเปิด-ปิดถี่)
 *
 * ทั้ง sensor + relay sync ไป Web App ตามปกติ
 *
 * หมายเหตุ: ใช้ mock sensor ที่ return ค่าคงที่
 * เพราะ DHT Arduino library ไม่รองรับ ESP-IDF framework
 */

#include <Synapta.h>

SynaptaSensor  temp("bedroom/temp");
SynaptaDigital fan ("bedroom/fan");

// mock sensor — แทนที่ด้วย ESP-IDF driver ของ sensor จริง
float readTemp() {
    static float t = 25.0f;   // ค่าคงที่ — ไม่เปิดหรือปิดพัดลม

    if (t > 30.0f && !fan.isOn()) fan.turnOn();
    if (t < 28.0f &&  fan.isOn()) fan.turnOff();

    return t;
}

extern "C" void app_main() {
    temp.every(5000, readTemp);

    Synapta.wifi("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
    Synapta.baseTopic("Mylab/smarthome");
    Synapta.start();
}
