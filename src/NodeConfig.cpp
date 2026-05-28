#include "NodeConfig.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* NS = "synapta-cfg";

static std::string nvsReadStr(nvs_handle_t h, const char* key, const char* def) {
    size_t len = 0;
    if (nvs_get_str(h, key, nullptr, &len) != ESP_OK) return def;
    std::string s(len, '\0');
    nvs_get_str(h, key, &s[0], &len);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

void NodeConfig::load() {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;

    wifiSSID     = nvsReadStr(h, "ssid",   "");
    wifiPassword = nvsReadStr(h, "pass",   "");
    mqttBroker   = nvsReadStr(h, "broker", "broker.hivemq.com");
    mqttUser     = nvsReadStr(h, "user",   "");
    mqttPassword = nvsReadStr(h, "mpass",  "");
    baseTopic    = nvsReadStr(h, "base",   "Mylab/smarthome");
    nodeId       = nvsReadStr(h, "nodeid", "");

    int32_t port = 8883;
    nvs_get_i32(h, "port", &port);
    mqttPort = (int)port;

    uint8_t tls = 1;
    nvs_get_u8(h, "tls", &tls);
    mqttTLS = (bool)tls;

    nvs_close(h);
}

void NodeConfig::save() const {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_str(h, "ssid",   wifiSSID.c_str());
    nvs_set_str(h, "pass",   wifiPassword.c_str());
    nvs_set_str(h, "broker", mqttBroker.c_str());
    nvs_set_str(h, "user",   mqttUser.c_str());
    nvs_set_str(h, "mpass",  mqttPassword.c_str());
    nvs_set_str(h, "base",   baseTopic.c_str());
    nvs_set_str(h, "nodeid", nodeId.c_str());
    nvs_set_i32(h, "port",   (int32_t)mqttPort);
    nvs_set_u8 (h, "tls",    (uint8_t)mqttTLS);
    nvs_commit(h);
    nvs_close(h);
}
