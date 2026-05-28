/*
 * 03_Sensor — sensor publish ค่าเป็นช่วงเวลา
 *
 * หมายเหตุ: ใช้ mock sensor ที่ return ค่าคงที่ 25.0°C
 * เพราะ DHT Arduino library ไม่รองรับ ESP-IDF framework
 * ถ้าต้องการ sensor จริง ให้ใช้ ESP-IDF RMT driver แทน
 *
 * Sensor publish อย่างเดียว ไม่รับ command
 */

#include <Synapta.h>

// mock sensor — แทนที่ด้วย ESP-IDF driver ของ sensor จริง
float readTemp() {
    return 25.0f;
}

// arg 2 = ชื่อสำหรับ Web App แสดง (รองรับภาษาไทย — เป็น UTF-8)
SynaptaSensor temp("bedroom/temp", "อุณหภูมิห้องนอน");

extern "C" void app_main() {
    temp.unit("°C");              // หน่วยที่เว็บจะแสดงข้างค่า
    temp.every(30000, readTemp);

    Synapta.wifi("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
    Synapta.baseTopic("Mylab/smarthome");
    Synapta.start();
}
