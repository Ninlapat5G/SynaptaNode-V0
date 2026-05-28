/*
 * 07_NvsCredentials — บันทึก WiFi/MQTT settings ลง NVS ครั้งเดียว
 *                     แล้ว begin() จะโหลดเองทุกครั้งที่ boot
 *
 * ใช้เมื่อไหร่: ไม่อยาก hardcode credential ใน code ทุก project
 *
 * วิธีใช้:
 *   1. uncomment configure(...) → upload → reboot → credential บันทึก
 *   2. comment configure(...) กลับ → upload ใหม่ → ใช้ begin() แทนได้เลย
 *
 * NVS เก็บข้ามการ flash sketch ใหม่ (เก็บใน flash region แยกจาก code)
 */

#include <Synapta.h>

SynaptaDigital relay("bedroom/relay");

void setup() {
    Serial.begin(115200);

    // ── ครั้งแรกเท่านั้น: uncomment + upload 1 ครั้ง แล้ว comment กลับ ──
    // Synapta.configure("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD", "Mylab/smarthome");

    // ── ครั้งต่อๆ ไป: โหลด credential จาก NVS อัตโนมัติ ──
    Synapta.begin();
}

void loop() {
    Synapta.loop();
}
