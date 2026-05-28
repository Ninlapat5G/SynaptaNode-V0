#include "SynaptaNode.h"
#include "SynaptaRegistry.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include <cstring>

SynaptaNodeClass Synapta;

// ── NVS init ──────────────────────────────────────────────────────────────────

static void _nvs_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

// ── WiFi event handler ────────────────────────────────────────────────────────

static EventGroupHandle_t s_wifiEG;
#define WIFI_CONNECTED_BIT BIT0

static void wifiEventHandler(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)        esp_wifi_connect();
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
    if (base == IP_EVENT   && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(s_wifiEG, WIFI_CONNECTED_BIT);
}

// ── V1 Fluent config ──────────────────────────────────────────────────────────

SynaptaNodeClass& SynaptaNodeClass::wifi(const char* ssid, const char* pass) {
    _cfg.wifiSSID = ssid; _cfg.wifiPassword = pass; return *this;
}
SynaptaNodeClass& SynaptaNodeClass::broker(const char* host, int port, bool tls) {
    _cfg.mqttBroker = host; _cfg.mqttPort = port; _cfg.mqttTLS = tls; return *this;
}
SynaptaNodeClass& SynaptaNodeClass::mqttAuth(const char* user, const char* pass) {
    _cfg.mqttUser = user; _cfg.mqttPassword = pass; return *this;
}
SynaptaNodeClass& SynaptaNodeClass::baseTopic(const char* base) {
    _cfg.baseTopic = base; return *this;
}
SynaptaNodeClass& SynaptaNodeClass::nodeId(const char* id) {
    _cfg.nodeId = id; return *this;
}

void SynaptaNodeClass::start() {
    _nvs_init();
    if (!_cfg.isValid()) {
        printf("[Synapta] ERROR: call wifi() and baseTopic() before start()\n");
        return;
    }
    _init();
}

// ── Legacy API ────────────────────────────────────────────────────────────────

void SynaptaNodeClass::begin(const char* ssid, const char* pass, const char* base) {
    _nvs_init();
    _cfg = NodeConfig(ssid, pass, base);
    _init();
}

void SynaptaNodeClass::begin() {
    _nvs_init();
    _cfg.load();
    if (!_cfg.isValid()) {
        printf("[Synapta] ERROR: no saved credentials — call configure() first\n");
        return;
    }
    _init();
}

void SynaptaNodeClass::configure(const char* ssid, const char* pass, const char* base) {
    _nvs_init();
    _cfg = NodeConfig(ssid, pass, base);
    _cfg.save();
    _init();
}

// ── Init ──────────────────────────────────────────────────────────────────────

void SynaptaNodeClass::_init() {
    _mutex = xSemaphoreCreateMutex();

    _devices.clear();
    for (auto* d : _SynaptaRegistry::devices()) {
        _devices.push_back(d);
        d->_loadPinConfig();
    }

    // WiFi
    s_wifiEG = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifiInitCfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifiInitCfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,   wifiEventHandler, nullptr);
    esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, wifiEventHandler, nullptr);

    wifi_config_t wCfg = {};
    strncpy((char*)wCfg.sta.ssid,     _cfg.wifiSSID.c_str(),     sizeof(wCfg.sta.ssid) - 1);
    strncpy((char*)wCfg.sta.password, _cfg.wifiPassword.c_str(), sizeof(wCfg.sta.password) - 1);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wCfg);
    esp_wifi_start();

    printf("[Synapta] WiFi connecting...\n");
    xEventGroupWaitBits(s_wifiEG, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    printf("[Synapta] WiFi connected\n");

    // MQTT
    std::string uri = (_cfg.mqttTLS ? "mqtts://" : "mqtt://")
                    + _cfg.mqttBroker + ":" + std::to_string(_cfg.mqttPort);
    std::string clientId    = "synapta-" + _macSuffix();
    std::string statusTopic = _statusTopic();

    // ต้องเก็บ string ไว้ให้ pointer ยังชี้ถึงได้ตลอด lifetime ของ client
    static std::string s_uri, s_clientId, s_statusTopic, s_user, s_pass;
    s_uri         = uri;
    s_clientId    = clientId;
    s_statusTopic = statusTopic;
    s_user        = _cfg.mqttUser;
    s_pass        = _cfg.mqttPassword;

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri              = s_uri.c_str();
    cfg.session.protocol_ver            = MQTT_PROTOCOL_V_5;
    cfg.credentials.client_id           = s_clientId.c_str();
    cfg.session.last_will.topic         = s_statusTopic.c_str();
    cfg.session.last_will.msg           = "offline";
    cfg.session.last_will.qos           = 1;
    cfg.session.last_will.retain        = 1;
    cfg.session.last_will.msg_len       = 7;

    if (!s_user.empty()) {
        cfg.credentials.username                = s_user.c_str();
        cfg.credentials.authentication.password = s_pass.c_str();
        // password_len removed in ESP-IDF 5.x — password is null-terminated
    }
    if (_cfg.mqttTLS) {
        cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }

    _client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(_client, MQTT_EVENT_ANY, _eventHandler, this);
    esp_mqtt_client_start(_client);

    printf("[Synapta] MQTT connecting (MQTT 5)...\n");

    // Background task — รัน message dispatch + device loop โดยไม่ต้องมี loop() ใน user code
    xTaskCreate([](void* arg) {
        auto* self = static_cast<SynaptaNodeClass*>(arg);
        while (true) {
            std::vector<_SynaptaMsg> inbox;
            xSemaphoreTake(self->_mutex, portMAX_DELAY);
            inbox.swap(self->_inbox);
            xSemaphoreGive(self->_mutex);
            for (auto& msg : inbox) self->_dispatch(msg);
            for (auto* d : self->_devices) d->_loop();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }, "synapta_main", 8192, this, 5, &_loopTask);
}

// ── MQTT event handler — runs in esp-mqtt internal FreeRTOS task ──────────────

void SynaptaNodeClass::_eventHandler(void* arg, esp_event_base_t,
                                     int32_t id, void* data) {
    auto* self  = static_cast<SynaptaNodeClass*>(arg);
    auto* event = static_cast<esp_mqtt_event_handle_t>(data);

    switch (id) {
        case MQTT_EVENT_CONNECTED:    self->_onConnected();    break;
        case MQTT_EVENT_DISCONNECTED: self->_onDisconnected(); break;
        case MQTT_EVENT_DATA:         self->_onData(event);    break;
        default: break;
    }
}

void SynaptaNodeClass::_onConnected() {
    _connected = true;
    printf("[Synapta] MQTT 5 connected\n");

    _publish(_statusTopic().c_str(), "online", true, 1);

    for (auto* d : _devices) {
        std::string cmd = d->_cmdTopic(_cfg.baseTopic);
        std::string cfg = d->_configTopic(_cfg.baseTopic);
        esp_mqtt_client_subscribe(_client, cmd.c_str(), 1);
        esp_mqtt_client_subscribe(_client, cfg.c_str(), 1);
        printf("[Synapta] Sub: %s\n", cmd.c_str());
    }

    for (auto* d : _devices) d->_reportState();
    _publishManifest();

    if (_cbConnect) _cbConnect();
}

void SynaptaNodeClass::_onDisconnected() {
    _connected = false;
    printf("[Synapta] MQTT disconnected — auto-reconnecting\n");
    if (_cbDisconnect) _cbDisconnect();
}

void SynaptaNodeClass::_onData(esp_mqtt_event_handle_t event) {
    _SynaptaMsg msg;
    msg.topic   = std::string(event->topic,   event->topic_len);
    msg.payload = std::string(event->data,    event->data_len);

#ifdef CONFIG_MQTT_PROTOCOL_5
    if (event->property) {
        if (event->property->response_topic && event->property->response_topic_len > 0) {
            msg.responseTopic = std::string(event->property->response_topic,
                                            event->property->response_topic_len);
        }
        if (event->property->correlation_data && event->property->correlation_data_len > 0) {
            const auto* cd = reinterpret_cast<const uint8_t*>(event->property->correlation_data);
            msg.corrData.assign(cd, cd + event->property->correlation_data_len);
        }
    }
#endif

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _inbox.push_back(std::move(msg));
    xSemaphoreGive(_mutex);
}

// ── Dispatch — runs in background loop task ───────────────────────────────────

void SynaptaNodeClass::_dispatch(const _SynaptaMsg& msg) {
    for (auto* d : _devices) {
        if (msg.topic == d->_cmdTopic(_cfg.baseTopic)) {
            d->_handleMessage(msg.payload.c_str());
            return;
        }
        if (msg.topic == d->_configTopic(_cfg.baseTopic)) {
            d->_handleConfig(
                msg.payload.c_str(),
                msg.responseTopic.empty() ? nullptr : msg.responseTopic.c_str(),
                msg.corrData.empty()      ? nullptr : msg.corrData.data(),
                msg.corrData.size()
            );
            return;
        }
    }
}

// ── Publish ───────────────────────────────────────────────────────────────────

bool SynaptaNodeClass::_publish(const char* topic, const char* payload,
                                bool retain, int qos, uint32_t expirySeconds) {
    if (!_client || !_connected) return false;

#ifdef CONFIG_MQTT_PROTOCOL_5
    if (expirySeconds > 0) {
        esp_mqtt5_publish_property_config_t prop = {};
        prop.message_expiry_interval = expirySeconds;
        esp_mqtt5_client_set_publish_property(_client, &prop);
    }
#endif

    int ret = esp_mqtt_client_publish(_client, topic, payload, 0, qos, retain ? 1 : 0);

#ifdef CONFIG_MQTT_PROTOCOL_5
    if (expirySeconds > 0) {
        esp_mqtt5_publish_property_config_t reset = {};
        esp_mqtt5_client_set_publish_property(_client, &reset);
    }
#endif

    return ret >= 0;
}

// ── Manifest ──────────────────────────────────────────────────────────────────

void SynaptaNodeClass::_publishManifest() {
    bool allCfg = true;
    std::string nId = _nodeId();

    std::string json = "{\"nodeId\":\"";
    json += nId;
    json += "\",\"baseTopic\":\"";
    json += _cfg.baseTopic;
    json += "\",\"fw\":\"3.0.0\",\"devices\":[";

    for (size_t i = 0; i < _devices.size(); i++) {
        if (i > 0) json += ',';
        json += _devices[i]->_manifestEntry(_cfg.baseTopic);
        if (!_devices[i]->isConfigured()) allCfg = false;
    }

    json += "],\"configured\":";
    json += allCfg ? "true" : "false";
    json += "}";

    std::string topic = _manifestTopic();
    // retain + 1 ชั่วโมง — manifest เก่าหายเองถ้า node ไม่ reconnect
    bool ok = _publish(topic.c_str(), json.c_str(), true, 1, 3600);
    printf("[Synapta] Manifest → %s %s\n", topic.c_str(), ok ? "OK" : "FAILED");
}

// ── Helpers ───────────────────────────────────────────────────────────────────

std::string SynaptaNodeClass::_macSuffix() const {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char buf[7];
    snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return std::string(buf);
}

std::string SynaptaNodeClass::_nodeId() const {
    return !_cfg.nodeId.empty() ? _cfg.nodeId : "node-" + _macSuffix();
}
std::string SynaptaNodeClass::_statusTopic()   const { return _cfg.baseTopic + "/nodes/" + _nodeId() + "/status"; }
std::string SynaptaNodeClass::_manifestTopic() const { return _cfg.baseTopic + "/nodes/" + _nodeId() + "/manifest"; }
