/*
 * 02_MultiDevice — หลาย device บน ESP32 ตัวเดียว + วิธีเขียน callback 2 แบบ
 *
 * Pin assignment: Web App → Edit แต่ละ device → ใส่ Pin → Save
 */

#include <Synapta.h>

SynaptaDigital relay ("bedroom/relay");
SynaptaAnalog  dimmer("bedroom/dimmer");


void onRelayChange(bool on) {
    printf(on ? "Relay: ON\n" : "Relay: OFF\n");
}


extern "C" void app_main() {
    // ตั้ง callback ก่อน start() เพื่อป้องกัน race condition
    relay.onCommand(onRelayChange);

    dimmer.gamma();   // gamma 2.2 สำหรับ LED ให้ตาเห็นเป็น linear

    dimmer.onValue([](int val) {
        printf("Dimmer: %d/255\n", val);
    });

    Synapta.wifi("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
    Synapta.baseTopic("Mylab/smarthome");
    Synapta.start();
}
