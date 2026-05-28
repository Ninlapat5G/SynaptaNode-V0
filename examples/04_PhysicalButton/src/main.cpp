/*
 * 04_PhysicalButton — ปุ่มกดจริงบน GPIO + sync ไป MQTT
 *
 * กดปุ่ม → toggle state → GPIO เปลี่ยนตาม → Web App UI อัพเดท
 * แม้ WiFi หลุด ปุ่มยังกดได้ปกติ — state sync เมื่อ MQTT กลับมา
 *
 * Pin assignment (output): Web App → Edit → ใส่ Pin → Save
 * Button pin (input): กำหนดใน attachButton() ด้านล่าง
 */

#include <Synapta.h>

SynaptaDigital lamp("bedroom/lamp");

void onLampChange(bool on) {
    printf(on ? "Lamp: ON\n" : "Lamp: OFF\n");
}

extern "C" void app_main() {
    lamp.attachButton(5);        // GPIO 5 — internal pull-up, debounce 50ms
    lamp.onCommand(onLampChange);

    Synapta.onConnect   ([]() { printf("[Synapta] Connected\n"); });
    Synapta.onDisconnect([]() { printf("[Synapta] Disconnected — button still works\n"); });

    Synapta.wifi("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
    Synapta.baseTopic("Mylab/smarthome");
    Synapta.start();
}
