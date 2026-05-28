#pragma once
#include <string>
#include <functional>
#include "SynaptaRegistry.h"
#include "esp_timer.h"

enum DeviceType { NODE_DIGITAL, NODE_ANALOG, NODE_SENSOR };

class SynaptaDevice {
public:
    static constexpr uint8_t NO_PIN = 255;

    // topic = full path ใต้ baseTopic เช่น "living-room/lamp"
    // name  = ชื่อสำหรับ Web App แสดงผล (optional — ไม่ใส่ เว็บจะ derive จาก topic)
    SynaptaDevice(const char* topic, DeviceType type, const char* name = "");

    // DIGITAL: fires on "true"/"on"/"1"/"toggle"/"false"/"off"/"0"
    void onCommand(std::function<void(bool)> cb);

    // ANALOG: fires on 0–255 value
    void onValue(std::function<void(int)> cb);

    // Auto-drive a GPIO pin
    void attachPin(uint8_t pin);   // DIGITAL: output HIGH/LOW
    void attachPWM(uint8_t pin);   // ANALOG: 8-bit PWM via LEDC

    // Active-low push button (internal pull-up, 50 ms debounce)
    void attachButton(uint8_t pin);

    // SENSOR: เรียก cb ทุก intervalMs ms แล้ว publish ค่าที่ได้
    void every(uint32_t intervalMs, std::function<float()> cb);

    void set(bool state);
    void set(int  value);

    void setFadeMs(uint32_t ms) { _fadeMs = ms; }
    void setGamma(float g);

    float value() const;

    // หน่วยของ sensor สำหรับให้ Web App แสดง เช่น "°C" (optional)
    void setUnit(const char* u) { _unit = u; }

    // Internal — เรียกโดย SynaptaNode
    void _handleMessage(const char* payload);
    void _handleConfig (const char* payload,
                        const char* response_topic,
                        const uint8_t* correlation_data,
                        size_t correlation_len = 0);
    void _loadPinConfig();
    void _reportState() { _publishState(); }
    void _loop();

    bool isConfigured() const { return _configured; }

    std::string _cmdTopic   (const std::string& base) const;
    std::string _stateTopic (const std::string& base) const;
    std::string _configTopic(const std::string& base) const;
    std::string _manifestEntry(const std::string& base) const;

    const std::string& getTopic() const { return _topic; }
    DeviceType         getType()  const { return _type; }
    const char*        typeName() const;

private:
    std::string _topic;
    DeviceType  _type;
    std::string _name;   // ชื่อสำหรับ Web App (ไม่ตั้งก็ได้ — เว็บ derive จาก topic)
    std::string _unit;   // หน่วยของ sensor เช่น "°C" (optional)

    bool  _stateBool  = false;
    float _stateFloat = 0;
    bool  _configured = false;
    int   _nvPin      = -1;

    std::function<void(bool)>  _cbDigital;
    std::function<void(int)>   _cbAnalog;
    std::function<float()>     _cbSensor;

    uint8_t _pin        = NO_PIN;
    int8_t  _pwmChannel = -1;

    uint32_t _fadeMs       = 200;
    int      _pwmTarget    = 0;
    int      _pwmCurrent   = 0;
    int      _fadeStartVal = 0;
    uint32_t _fadeStartMs  = 0;
    bool     _useGamma     = false;

    static uint8_t _gammaLut[256];
    static float   _gammaValue;

    uint8_t  _btnPin         = NO_PIN;
    bool     _btnLastReading = true;
    bool     _btnPressed     = false;
    uint32_t _btnDebounceMs  = 0;

    uint32_t _interval   = 0;
    uint32_t _lastReport = 0;

    // djb2 hash ของ topic → 8 hex chars (อยู่ใน 15-char limit ของ NVS)
    std::string _nvKey() const;

    bool _parseBool(const char* s) const;
    void _executeDigital(bool on);
    void _executeAnalog (int  val);
    void _publishState  ();

    void _writePWM(int v);
    void _tickFade();
};
