#pragma once
#include <string>
#include <vector>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "mqtt_client.h"
#include "NodeConfig.h"
#include "SynaptaDevice.h"

// Incoming MQTT message — filled by MQTT task, consumed by background loop task
struct _SynaptaMsg {
    std::string topic;
    std::string payload;
    std::string responseTopic;           // MQTT 5: read from incoming properties
    std::vector<uint8_t> corrData;       // MQTT 5: correlation data
};

class SynaptaNodeClass {
public:
    // ── V1 Fluent config — เรียกก่อน start() ─────────────────────────────────
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
    bool isConnected() { return _connected; }

    void onConnect   (std::function<void()> cb) { _cbConnect    = cb; }
    void onDisconnect(std::function<void()> cb) { _cbDisconnect = cb; }

    // Internal — called by SynaptaDevice
    bool _publish(const char* topic, const char* payload,
                  bool retain = true, int qos = 1, uint32_t expirySeconds = 0);

    const NodeConfig& config() const { return _cfg; }

private:
    NodeConfig _cfg;
    esp_mqtt_client_handle_t _client = nullptr;
    volatile bool            _connected = false;

    std::function<void()> _cbConnect;
    std::function<void()> _cbDisconnect;

    SemaphoreHandle_t          _mutex = nullptr;
    std::vector<_SynaptaMsg>   _inbox;
    std::vector<SynaptaDevice*> _devices;
    TaskHandle_t               _loopTask = nullptr;

    void _init();
    void _publishManifest();
    std::string _nodeId()        const;
    std::string _statusTopic()   const;
    std::string _manifestTopic() const;
    std::string _macSuffix()     const;

    static void _eventHandler(void* arg, esp_event_base_t base,
                              int32_t id, void* data);
    void _onConnected();
    void _onDisconnected();
    void _onData(esp_mqtt_event_handle_t event);

    void _dispatch(const _SynaptaMsg& msg);
};

extern SynaptaNodeClass Synapta;
