#include "NodeConfig.h"
#include <Preferences.h>

static const char* NS = "synapta-cfg";   // Preferences namespace

void NodeConfig::load() {
    Preferences prefs;
    prefs.begin(NS, true);  // read-only

    wifiSSID     = prefs.getString("ssid",   "");
    wifiPassword = prefs.getString("pass",   "");
    mqttBroker   = prefs.getString("broker", "broker.hivemq.com");
    mqttPort     = prefs.getInt   ("port",   8883);
    mqttTLS      = prefs.getBool  ("tls",    true);
    mqttUser     = prefs.getString("user",   "");
    mqttPassword = prefs.getString("mpass",  "");
    baseTopic    = prefs.getString("base",   "Mylab/smarthome");
    nodeId       = prefs.getString("nodeid", "");

    prefs.end();
}

void NodeConfig::save() const {
    Preferences prefs;
    prefs.begin(NS, false);  // read-write

    prefs.putString("ssid",   wifiSSID);
    prefs.putString("pass",   wifiPassword);
    prefs.putString("broker", mqttBroker);
    prefs.putInt   ("port",   mqttPort);
    prefs.putBool  ("tls",    mqttTLS);
    prefs.putString("user",   mqttUser);
    prefs.putString("mpass",  mqttPassword);
    prefs.putString("base",   baseTopic);
    prefs.putString("nodeid", nodeId);

    prefs.end();
}
