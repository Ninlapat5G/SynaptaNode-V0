#include "SynaptaDevice.h"
#include "SynaptaNode.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include <cmath>
#include <cstring>
#include <string>

static const char* PIN_NS = "syn-pins";

static inline uint32_t _millis() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

uint8_t SynaptaDevice::_gammaLut[256];
float   SynaptaDevice::_gammaValue = 0.0f;

SynaptaDevice::SynaptaDevice(const char* topic, DeviceType type)
    : _topic(topic), _type(type)
{
    _SynaptaRegistry::devices().push_back(this);
}

void SynaptaDevice::onCommand(std::function<void(bool)> cb) { _cbDigital = cb; }
void SynaptaDevice::onValue  (std::function<void(int)>  cb) { _cbAnalog  = cb; }

void SynaptaDevice::attachPin(uint8_t pin) {
    _pin = pin;
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)pin, 0);
}

void SynaptaDevice::attachPWM(uint8_t pin) {
    _pin = pin;
    static uint8_t nextChannel = 0;
    _pwmChannel = (int8_t)(nextChannel++ & 0x07);

    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer.timer_num       = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.freq_hz         = 5000;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {};
    ch.gpio_num   = pin;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = (ledc_channel_t)_pwmChannel;
    ch.timer_sel  = LEDC_TIMER_0;
    ch.intr_type  = LEDC_INTR_DISABLE;
    ch.duty       = 0;
    ch.hpoint     = 0;
    ledc_channel_config(&ch);
}

void SynaptaDevice::attachButton(uint8_t pin) {
    _btnPin = pin;
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io);
}

void SynaptaDevice::every(uint32_t intervalMs, std::function<float()> cb) {
    _interval = intervalMs;
    _cbSensor = cb;
}

void SynaptaDevice::set(bool state) {
    _executeDigital(state);
    _publishState();
}

void SynaptaDevice::set(int value) {
    _executeAnalog(value);
    _publishState();
}

float SynaptaDevice::value() const {
    return (_type == NODE_DIGITAL) ? (_stateBool ? 1.0f : 0.0f) : _stateFloat;
}

// ── Topic helpers ─────────────────────────────────────────────────────────────

std::string SynaptaDevice::_cmdTopic   (const std::string& base) const { return base + "/" + _topic + "/set"; }
std::string SynaptaDevice::_stateTopic (const std::string& base) const { return base + "/" + _topic + "/state"; }
std::string SynaptaDevice::_configTopic(const std::string& base) const { return base + "/" + _topic + "/config"; }

const char* SynaptaDevice::typeName() const {
    if (_type == NODE_DIGITAL) return "digital";
    if (_type == NODE_ANALOG)  return "analog";
    return "sensor";
}

// ── Manifest entry ────────────────────────────────────────────────────────────

std::string SynaptaDevice::_manifestEntry(const std::string& base) const {
    std::string j = "{\"topic\":\"";
    j += _topic;
    j += "\",\"type\":\"";
    j += typeName();
    j += "\",\"configured\":";
    j += _configured ? "true" : "false";
    if (_configured && _nvPin >= 0) {
        j += ",\"pin\":";
        j += std::to_string(_nvPin);
    }
    j += ",\"stateTopic\":\"";
    j += _stateTopic(base);
    j += "\"";
    if (_type != NODE_SENSOR) {
        j += ",\"cmdTopic\":\"";
        j += _cmdTopic(base);
        j += "\",\"configTopic\":\"";
        j += _configTopic(base);
        j += "\"";
    }
    j += "}";
    return j;
}

// ── Command handler ───────────────────────────────────────────────────────────

void SynaptaDevice::_handleMessage(const char* payload) {
    if (_type == NODE_DIGITAL) {
        bool on = _parseBool(payload);
        _executeDigital(on);
        _publishState();
    } else if (_type == NODE_ANALOG) {
        int val = atoi(payload);
        if (val < 0)   val = 0;
        if (val > 255) val = 255;
        _executeAnalog(val);
        _publishState();
    }
}

// ── Config handler ────────────────────────────────────────────────────────────

void SynaptaDevice::_handleConfig(const char* payload, const char* response_topic,
                                   const uint8_t* correlation_data, size_t correlation_len) {
    std::string p(payload);

    size_t pinIdx = p.find("\"pin\":");
    if (pinIdx == std::string::npos) return;
    int pin = std::stoi(p.substr(pinIdx + 6));
    if (pin < 0 || pin > 48) return;

    bool isPwm = (p.find("\"pwm\"") != std::string::npos ||
                  p.find("\"analog\"") != std::string::npos);

    std::string key  = _nvKey();
    std::string keyT = key + "t";

    nvs_handle_t h;
    int32_t oldPin   = -1;
    uint8_t oldIsPwm = 0;
    if (nvs_open(PIN_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, key.c_str(),  &oldPin);
        nvs_get_u8 (h, keyT.c_str(), &oldIsPwm);
        nvs_close(h);
    }

    bool unchanged = (oldPin == pin && (bool)oldIsPwm == isPwm);

    if (!unchanged) {
        if (nvs_open(PIN_NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_i32(h, key.c_str(),  (int32_t)pin);
            nvs_set_u8 (h, keyT.c_str(), (uint8_t)isPwm);
            nvs_commit(h);
            nvs_close(h);
        }
        printf("[Synapta] Config saved: %s → pin %d (%s)\n",
               _topic.c_str(), pin, isPwm ? "pwm" : "digital");

        if (isPwm) attachPWM((uint8_t)pin);
        else       attachPin((uint8_t)pin);

        _configured = true;
        _nvPin      = pin;
    } else {
        printf("[Synapta] Config unchanged for %s — skip NVS write\n", _topic.c_str());
    }

    if (response_topic && response_topic[0] != '\0') {
        std::string ack = "{\"ok\":true,\"applied\":{\"pin\":";
        ack += std::to_string(pin);
        ack += ",\"type\":\"";
        ack += isPwm ? "pwm" : "digital";
        ack += "\"}}";
        Synapta._publish(response_topic, ack.c_str(), false, 1);
        printf("[Synapta] Config ACK → %s\n", response_topic);
    }
}

// ── Boot: load pin from NVS ───────────────────────────────────────────────────

void SynaptaDevice::_loadPinConfig() {
    std::string key  = _nvKey();
    std::string keyT = key + "t";

    nvs_handle_t h;
    if (nvs_open(PIN_NS, NVS_READONLY, &h) != ESP_OK) return;

    int32_t pin   = -1;
    uint8_t isPwm = 0;
    nvs_get_i32(h, key.c_str(),  &pin);
    nvs_get_u8 (h, keyT.c_str(), &isPwm);
    nvs_close(h);

    if (pin < 0 || pin > 48) return;

    printf("[Synapta] Loaded pin config: %s → pin %d (%s)\n",
           _topic.c_str(), (int)pin, isPwm ? "pwm" : "digital");

    if (isPwm) attachPWM((uint8_t)pin);
    else       attachPin((uint8_t)pin);

    _configured = true;
    _nvPin      = (int)pin;
}

// ── NVS key ───────────────────────────────────────────────────────────────────

std::string SynaptaDevice::_nvKey() const {
    uint32_t h = 5381;
    for (const char* c = _topic.c_str(); *c; c++) h = ((h << 5) + h) + *c;
    char buf[9];
    snprintf(buf, sizeof(buf), "%08lx", (unsigned long)h);
    return std::string(buf);
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void SynaptaDevice::_loop() {
    if (_type == NODE_SENSOR && _cbSensor && _interval > 0) {
        if (_millis() - _lastReport >= _interval) {
            _lastReport = _millis();
            _stateFloat = _cbSensor();
            _publishState();
        }
    }

    if (_type == NODE_ANALOG) _tickFade();

    if (_btnPin != NO_PIN) {
        bool reading = (gpio_get_level((gpio_num_t)_btnPin) == 0);

        if (reading != _btnLastReading) {
            _btnDebounceMs = _millis();
        }

        if (_millis() - _btnDebounceMs > 50) {
            if (reading != _btnPressed) {
                _btnPressed = reading;
                if (_btnPressed) {
                    _stateBool = !_stateBool;
                    _executeDigital(_stateBool);
                    _publishState();
                }
            }
        }

        _btnLastReading = reading;
    }
}

// ── Execute helpers ───────────────────────────────────────────────────────────

void SynaptaDevice::_executeDigital(bool on) {
    _stateBool = on;
    if (_pin != NO_PIN) gpio_set_level((gpio_num_t)_pin, on ? 1 : 0);
    if (_cbDigital) _cbDigital(on);
}

void SynaptaDevice::_executeAnalog(int val) {
    _stateFloat = val;
    _pwmTarget  = val;

    if (_fadeMs == 0 || _pin == NO_PIN) {
        _pwmCurrent = val;
        _writePWM(val);
    } else {
        _fadeStartVal = _pwmCurrent;
        _fadeStartMs  = _millis();
    }

    if (_cbAnalog) _cbAnalog(val);
}

// ── PWM helpers ───────────────────────────────────────────────────────────────

void SynaptaDevice::_writePWM(int v) {
    if (_pin == NO_PIN || _pwmChannel < 0) return;
    int actual = (_useGamma && v >= 0 && v <= 255) ? _gammaLut[v] : v;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_pwmChannel, (uint32_t)actual);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)_pwmChannel);
}

void SynaptaDevice::setGamma(float g) {
    if (g <= 1.0f) { _useGamma = false; return; }
    _useGamma = true;
    if (g != _gammaValue) {
        _gammaValue = g;
        for (int i = 0; i < 256; i++) {
            float n = (float)i / 255.0f;
            _gammaLut[i] = (uint8_t)(powf(n, g) * 255.0f + 0.5f);
        }
    }
}

void SynaptaDevice::_tickFade() {
    if (_fadeMs == 0 || _pin == NO_PIN) return;
    if (_pwmCurrent == _pwmTarget)      return;

    uint32_t elapsed = _millis() - _fadeStartMs;
    int next;
    if (elapsed >= _fadeMs) {
        next = _pwmTarget;
    } else {
        long delta = (long)(_pwmTarget - _fadeStartVal) * (long)elapsed;
        next = _fadeStartVal + (int)(delta / (long)_fadeMs);
    }

    if (next != _pwmCurrent) {
        _pwmCurrent = next;
        _writePWM(next);
    }
}

// ── State publish ─────────────────────────────────────────────────────────────

void SynaptaDevice::_publishState() {
    const std::string& base = Synapta.config().baseTopic;
    std::string payload;
    if (_type == NODE_DIGITAL) {
        payload = _stateBool ? "true" : "false";
    } else if (_type == NODE_ANALOG) {
        payload = std::to_string((int)_stateFloat);
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", _stateFloat);
        payload = buf;
    }
    // retain + 5 นาที expiry — state เก่าหายเองถ้า node ไม่ reconnect
    Synapta._publish(_stateTopic(base).c_str(), payload.c_str(), true, 1, 300);
}

// ── Bool parser ───────────────────────────────────────────────────────────────

bool SynaptaDevice::_parseBool(const char* s) const {
    std::string str(s);
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return false;
    size_t end = str.find_last_not_of(" \t\n\r");
    str = str.substr(start, end - start + 1);

    if (strcasecmp(str.c_str(), "toggle") == 0) return !_stateBool;
    return strcasecmp(str.c_str(), "true") == 0 ||
           strcasecmp(str.c_str(), "on")   == 0 ||
           str == "1";
}
