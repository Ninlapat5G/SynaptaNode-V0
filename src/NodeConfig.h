#pragma once
#include <string>

// Holds all connection settings for the node.
// Can be loaded from / saved to NVS so credentials survive reboots.
struct NodeConfig {
    std::string wifiSSID;
    std::string wifiPassword;

    std::string mqttBroker   = "broker.hivemq.com";
    int         mqttPort     = 8883;
    bool        mqttTLS      = true;
    std::string mqttUser;
    std::string mqttPassword;

    std::string baseTopic = "Mylab/smarthome";
    std::string nodeId;

    // Hardcode credentials directly in the sketch (good for dev/testing)
    NodeConfig(const char* ssid, const char* pass, const char* base)
        : wifiSSID(ssid), wifiPassword(pass), baseTopic(base) {}

    // Load credentials from NVS (use with Synapta.begin())
    NodeConfig() = default;

    void load();
    void save() const;

    bool isValid() const {
        return !wifiSSID.empty() && !mqttBroker.empty();
    }
};
