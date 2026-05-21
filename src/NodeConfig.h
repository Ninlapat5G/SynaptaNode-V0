#pragma once
#include <Arduino.h>

// Holds all connection settings for the node.
// Can be loaded from / saved to NVRAM (Preferences) so credentials
// survive reboots without being hardcoded in the sketch.
struct NodeConfig {
    String wifiSSID;
    String wifiPassword;

    String mqttBroker   = "broker.hivemq.com";
    int    mqttPort     = 8883;       // 8883 = TLS, 1883 = plain
    bool   mqttTLS      = true;
    String mqttUser;
    String mqttPassword;

    // Must match "Base Topic" in Web App Settings → MQTT
    String baseTopic    = "Mylab/smarthome";

    // Optional: unique name for this node, used in Dynamic Rules topics.
    // Auto-derived from MAC address if left empty.
    String nodeId;

    // ── Constructors ─────────────────────────────────────────────────────────

    // Hardcode credentials directly in the sketch (good for dev/testing)
    NodeConfig(const char* ssid, const char* pass, const char* base)
        : wifiSSID(ssid), wifiPassword(pass), baseTopic(base) {}

    // Load credentials from NVRAM (use with Synapta.begin())
    NodeConfig() = default;

    // ── Persistence ──────────────────────────────────────────────────────────

    void load();                // Read from NVRAM
    void save() const;          // Write to NVRAM

    bool isValid() const {
        return wifiSSID.length() > 0 && mqttBroker.length() > 0;
    }
};
