#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_mqtt_client.h"
#include "mqtt5_client.h"
#include "NodeConfig.h"
#include "SynaptaDevice.h"

// Incoming MQTT message — filled by MQTT task, consumed by Arduino loop task
struct _SynaptaMsg {
    String topic;
    String payload;
    String responseTopic;           // MQTT 5: read from incoming properties
    std::vector<uint8_t> corrData;  // MQTT 5: correlation data
};

class SynaptaNodeClass {
public:
    // ── V1 Fluent config — เรียกใน setup() ก่อน start() ──────────────────────
    SynaptaNodeClass& wifi      (const char* ssid, const char* pass);
    SynaptaNodeClass& broker    (const char* host, int port = 8883, bool tls = true);
    SynaptaNodeClass& mqttAuth  (const char* user, const char* pass);
    SynaptaNodeClass& baseTopic (const char* base);
    SynaptaNodeClass& nodeId    (const char* id);
    void              start();

    // ── Legacy API ────────────────────────────────────────────────────────────
    void begin(const char* ssid, const char* pass, const char* base);
    void begin();
    void configure(const char* ssid, const char* pass, const char* base);

    // ── Runtime ──────────────────────────────────────────────────────────────
    void loop();
    bool isConnected() { return _connected; }

    void onConnect   (std::function<void()> cb) { _cbConnect    = cb; }
    void onDisconnect(std::function<void()> cb) { _cbDisconnect = cb; }

    // Internal — called by SynaptaDevice
    // expirySeconds > 0 → MQTT 5 messageExpiryInterval (ถนอม broker จาก retain เก่า)
    bool _publish(const char* topic, const char* payload,
                  bool retain = true, int qos = 1, uint32_t expirySeconds = 0);

    const NodeConfig& config() const { return _cfg; }

private:
    NodeConfig _cfg;
    esp_mqtt_client_handle_t _client = nullptr;
    volatile bool            _connected = false;

    std::function<void()> _cbConnect;
    std::function<void()> _cbDisconnect;

    // inbox: MQTT task เขียน, loop() task อ่าน — คุ้มครองด้วย _mutex
    SemaphoreHandle_t        _mutex = nullptr;
    std::vector<_SynaptaMsg> _inbox;
    std::vector<SynaptaDevice*> _devices;

    // ── Init helpers ──────────────────────────────────────────────────────────
    void _init();
    void _publishManifest();
    String _nodeId()        const;
    String _statusTopic()   const;
    String _manifestTopic() const;
    String _macSuffix()     const;

    // ── MQTT 5 event handler (runs in esp-mqtt internal task) ─────────────────
    static void _eventHandler(void* arg, esp_event_base_t base,
                              int32_t id, void* data);
    void _onConnected();
    void _onDisconnected();
    void _onData(esp_mqtt_event_handle_t event);

    // ── Dispatch (runs in Arduino loop task) ──────────────────────────────────
    void _dispatch(const _SynaptaMsg& msg);
};

extern SynaptaNodeClass Synapta;
