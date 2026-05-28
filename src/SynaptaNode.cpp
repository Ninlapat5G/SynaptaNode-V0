#include "SynaptaNode.h"
#include "SynaptaRegistry.h"

SynaptaNodeClass Synapta;

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
    if (!_cfg.isValid()) {
        Serial.println("[Synapta] ERROR: call wifi() and baseTopic() before start()");
        return;
    }
    _init();
}

// ── Legacy API ────────────────────────────────────────────────────────────────

void SynaptaNodeClass::begin(const char* ssid, const char* pass, const char* base) {
    _cfg = NodeConfig(ssid, pass, base);
    _init();
}

void SynaptaNodeClass::begin() {
    _cfg.load();
    if (!_cfg.isValid()) {
        Serial.println("[Synapta] ERROR: no saved credentials — call configure() first");
        return;
    }
    _init();
}

void SynaptaNodeClass::configure(const char* ssid, const char* pass, const char* base) {
    _cfg = NodeConfig(ssid, pass, base);
    _cfg.save();
    _init();
}

// ── Init ──────────────────────────────────────────────────────────────────────

void SynaptaNodeClass::_init() {
    _mutex = xSemaphoreCreateMutex();

    // รวบรวม device ที่ลงทะเบียนไว้ผ่าน constructor (global scope)
    _devices.clear();
    for (auto* d : _SynaptaRegistry::devices()) {
        _devices.push_back(d);
        d->_loadPinConfig();
    }

    // WiFi — block จนกว่าจะเชื่อมต่อสำเร็จ
    WiFi.setAutoReconnect(true);
    WiFi.begin(_cfg.wifiSSID.c_str(), _cfg.wifiPassword.c_str());
    Serial.print("[Synapta] WiFi connecting");
    while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
    Serial.println(" OK  IP: " + WiFi.localIP().toString());

    // สร้าง MQTT URI
    String uri = (_cfg.mqttTLS ? String("mqtts://") : String("mqtt://"))
               + _cfg.mqttBroker + ":" + String(_cfg.mqttPort);
    String clientId    = "synapta-" + _macSuffix();
    String statusTopic = _statusTopic();

    // ต้องเก็บ String เหล่านี้ไว้ให้ pointer ยังชี้ถึงได้ตลอด lifetime ของ client
    // (esp_mqtt_client_init ใช้ pointer โดยตรง ไม่ copy ค่า)
    static String s_uri, s_clientId, s_statusTopic, s_user, s_pass;
    s_uri         = uri;
    s_clientId    = clientId;
    s_statusTopic = statusTopic;
    s_user        = _cfg.mqttUser;
    s_pass        = _cfg.mqttPassword;

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri                       = s_uri.c_str();
    cfg.session.protocol_ver                     = MQTT_PROTOCOL_V_5;
    cfg.credentials.client_id                    = s_clientId.c_str();
    cfg.session.last_will.topic                  = s_statusTopic.c_str();
    cfg.session.last_will.msg                    = "offline";
    cfg.session.last_will.qos                    = 1;
    cfg.session.last_will.retain                 = 1;
    cfg.session.last_will.msg_len                = 7; // strlen("offline")

    if (!s_user.isEmpty()) {
        cfg.credentials.username                          = s_user.c_str();
        cfg.credentials.authentication.password          = s_pass.c_str();
        cfg.credentials.authentication.password_len      = s_pass.length();
    }
    if (_cfg.mqttTLS) {
        // skip certificate CN check สำหรับ public broker ที่ไม่ได้ใช้ custom cert
        cfg.broker.verification.skip_cert_common_name_check = true;
    }

    _client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(_client, ESP_EVENT_ANY_ID, _eventHandler, this);
    esp_mqtt_client_start(_client);

    Serial.println("[Synapta] MQTT connecting (MQTT 5)...");
}

// ── Runtime loop ──────────────────────────────────────────────────────────────
// เรียกใน loop() ทุก cycle — ดึง message จาก inbox + รัน device logic

void SynaptaNodeClass::loop() {
    // swap inbox ออกมา (short critical section)
    std::vector<_SynaptaMsg> inbox;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    inbox.swap(_inbox);
    xSemaphoreGive(_mutex);

    for (auto& msg : inbox) _dispatch(msg);
    for (auto* d : _devices) d->_loop();
}

// ── MQTT 5 event handler — runs in esp-mqtt internal FreeRTOS task ─────────────

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
    Serial.println("[Synapta] MQTT 5 connected");

    // publish online status (retain, ไม่ใส่ expiry — ต้องการให้อยู่ถาวร)
    _publish(_statusTopic().c_str(), "online", true, 1);

    // subscribe command + config topic ของทุก device
    for (auto* d : _devices) {
        String cmd = d->_cmdTopic(_cfg.baseTopic);
        String cfg = d->_configTopic(_cfg.baseTopic);
        esp_mqtt_client_subscribe(_client, cmd.c_str(), 1);
        esp_mqtt_client_subscribe(_client, cfg.c_str(), 1);
        Serial.printf("[Synapta] Sub: %s\n", cmd.c_str());
    }

    // report state + manifest
    for (auto* d : _devices) d->_reportState();
    _publishManifest();

    if (_cbConnect) _cbConnect();
}

void SynaptaNodeClass::_onDisconnected() {
    _connected = false;
    Serial.println("[Synapta] MQTT disconnected — auto-reconnecting");
    if (_cbDisconnect) _cbDisconnect();
}

void SynaptaNodeClass::_onData(esp_mqtt_event_handle_t event) {
    _SynaptaMsg msg;
    msg.topic   = String(event->topic,   event->topic_len);
    msg.payload = String(event->data,    event->data_len);

    // MQTT 5: อ่าน responseTopic + correlationData จาก properties
    if (event->property) {
        if (event->property->response_topic) {
            msg.responseTopic = event->property->response_topic;
        }
        if (event->property->correlation_data && event->property->correlation_data_len > 0) {
            const auto* cd = reinterpret_cast<const uint8_t*>(event->property->correlation_data);
            msg.corrData.assign(cd, cd + event->property->correlation_data_len);
        }
    }

    // ส่งเข้า inbox ให้ loop() ประมวลผลใน Arduino task
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _inbox.push_back(std::move(msg));
    xSemaphoreGive(_mutex);
}

// ── Dispatch — runs in Arduino loop task ─────────────────────────────────────

void SynaptaNodeClass::_dispatch(const _SynaptaMsg& msg) {
    for (auto* d : _devices) {
        if (msg.topic == d->_cmdTopic(_cfg.baseTopic)) {
            d->_handleMessage(msg.payload.c_str());
            return;
        }
        if (msg.topic == d->_configTopic(_cfg.baseTopic)) {
            d->_handleConfig(
                msg.payload.c_str(),
                msg.responseTopic.isEmpty() ? nullptr : msg.responseTopic.c_str(),
                msg.corrData.empty()        ? nullptr : msg.corrData.data(),
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

    if (expirySeconds > 0) {
        esp_mqtt5_publish_property_config_t prop = {};
        prop.message_expiry_interval = expirySeconds;
        esp_mqtt5_client_set_publish_property(_client, &prop);
    }

    int ret = esp_mqtt_client_publish(_client, topic, payload, 0, qos, retain ? 1 : 0);

    if (expirySeconds > 0) {
        // reset ให้ publish ถัดไปไม่ได้รับ expiry โดยไม่ตั้งใจ
        esp_mqtt5_publish_property_config_t reset = {};
        esp_mqtt5_client_set_publish_property(_client, &reset);
    }

    return ret >= 0;
}

// ── Manifest ──────────────────────────────────────────────────────────────────

void SynaptaNodeClass::_publishManifest() {
    bool allCfg = true;
    String nId  = _nodeId();

    String json = "{\"nodeId\":\"";
    json += nId;
    json += "\",\"baseTopic\":\"";
    json += _cfg.baseTopic;
    json += "\",\"fw\":\"2.0.0\",\"devices\":[";

    for (size_t i = 0; i < _devices.size(); i++) {
        if (i > 0) json += ',';
        json += _devices[i]->_manifestEntry(_cfg.baseTopic);
        if (!_devices[i]->isConfigured()) allCfg = false;
    }

    json += "],\"configured\":";
    json += allCfg ? "true" : "false";
    json += "}";

    String topic = _manifestTopic();
    // retain + 1 ชั่วโมง — manifest เก่าหายเองถ้า node ไม่ reconnect
    bool ok = _publish(topic.c_str(), json.c_str(), true, 1, 3600);
    Serial.printf("[Synapta] Manifest → %s %s\n", topic.c_str(), ok ? "OK" : "FAILED");
}

// ── Helpers ───────────────────────────────────────────────────────────────────

String SynaptaNodeClass::_macSuffix() const {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[7];
    snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(buf);
}

String SynaptaNodeClass::_nodeId() const {
    return _cfg.nodeId.length() > 0 ? _cfg.nodeId : "node-" + _macSuffix();
}
String SynaptaNodeClass::_statusTopic()   const { return _cfg.baseTopic + "/nodes/" + _nodeId() + "/status"; }
String SynaptaNodeClass::_manifestTopic() const { return _cfg.baseTopic + "/nodes/" + _nodeId() + "/manifest"; }
