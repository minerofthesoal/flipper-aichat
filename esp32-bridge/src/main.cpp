// Flipper AI Chat - ESP32-S2 Devboard bridge
//
// Speaks the flat pipe-delimited protocol (docs/PROTOCOL.md) to the Flipper
// over UART0, and OpenAI-compatible chat-completions to LM Studio over
// WiFi. LM Studio is expected to be reachable through Tailscale (either a
// bare tailnet IP, or a hostname exposed via `tailscale serve`), so this
// firmware never needs to know anything about Tailscale itself - it's just
// talking HTTP to an address on the WiFi network it's joined.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ImprovWiFiLibrary.h>
#include "config.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0-dev"
#endif

Preferences prefs;
String wifiSsid;
String wifiPass;
String serverUrl;
String currentModel = "auto";

ImprovWiFi improvSerial(&Serial);

static void sendLine(const String& line) {
    Serial.println(line);
}

static String sanitizeForWire(const String& in) {
    String out = in;
    out.replace("\r", "");
    out.replace("\n", "\\n");
    return out;
}

static void loadConfig() {
    prefs.begin("aichat", false);
    wifiSsid = prefs.getString("ssid", DEFAULT_WIFI_SSID);
    wifiPass = prefs.getString("pass", DEFAULT_WIFI_PASS);
    serverUrl = prefs.getString("server", DEFAULT_LM_STUDIO_URL);
}

// ---- Improv WiFi (browser-driven provisioning, see web-installer/) --------
//
// While the device has no WiFi (fresh flash, or after a REPROVISION), the
// main loop hands the serial port entirely to improvSerial.handleSerial()
// so the web installer's Improv flow can talk to it. Our own CHAT/etc. text
// protocol and Improv's binary framing can't share the port at the same
// time without corrupting each other, so we simply gate on WiFi.status():
// no WiFi yet -> Improv owns the port; WiFi connected -> our protocol owns
// the port. See docs/PROTOCOL.md and docs/SETUP.md for the full story.

static void onImprovWiFiErrorCb(ImprovTypes::Error err) {
    // Deliberately not calling sendLine() here: while this fires we're still
    // inside the Improv binary session, and interleaving ASCII text would
    // corrupt it from the browser's point of view. The web installer UI
    // surfaces its own error state to the user.
    (void)err;
}

static void onImprovWiFiConnectedCb(const char* ssid, const char* password) {
    wifiSsid = String(ssid);
    wifiPass = String(password);
    prefs.putString("ssid", wifiSsid);
    prefs.putString("pass", wifiPass);
}

static void connectWifiIfNeeded() {
    if(WiFi.status() == WL_CONNECTED) return;
    if(wifiSsid.length() == 0) return; // nothing stored yet, wait for Improv
    improvSerial.tryConnectToWifi(wifiSsid.c_str(), wifiPass.c_str());
}


static void handleSetWifi(const String& rest) {
    int sep = rest.indexOf('|');
    if(sep < 0) {
        sendLine("ERR|SETWIFI needs ssid|pass");
        return;
    }
    wifiSsid = rest.substring(0, sep);
    wifiPass = rest.substring(sep + 1);
    prefs.putString("ssid", wifiSsid);
    prefs.putString("pass", wifiPass);
    sendLine("INFO|connecting to wifi " + wifiSsid);
    WiFi.disconnect();
    connectWifiIfNeeded();
    if(WiFi.status() == WL_CONNECTED) {
        sendLine("INFO|wifi up, ip " + WiFi.localIP().toString());
    } else {
        sendLine("ERR|wifi connect failed");
    }
}

static void handleReprovision() {
    // Drops WiFi so the next loop() iteration hands the serial port back to
    // Improv, letting the web installer's "Configure WiFi" flow run again
    // against an already-flashed, already-running device.
    sendLine("INFO|reprovisioning, reconnect the web installer now");
    WiFi.disconnect();
}

static void handleSetServer(const String& url) {
    serverUrl = url;
    prefs.putString("server", serverUrl);
    sendLine("INFO|server set to " + serverUrl);
}

static void handleListModels() {
    if(serverUrl.length() == 0) {
        sendLine("ERR|no server url configured, send SETSERVER|http://host:port");
        return;
    }
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.begin(serverUrl + "/v1/models");
    int code = http.GET();
    if(code != 200) {
        sendLine("ERR|models request failed, http " + String(code));
        http.end();
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if(err) {
        sendLine("ERR|bad json from /v1/models");
        return;
    }

    String csv;
    JsonArray data = doc["data"].as<JsonArray>();
    for(JsonObject item : data) {
        const char* id = item["id"] | "";
        if(strlen(id) == 0) continue;
        if(csv.length() > 0) csv += ",";
        csv += id;
    }
    sendLine("MODELS|" + csv);
}

static void handleChat(const String& text) {
    if(serverUrl.length() == 0) {
        sendLine("ERR|no server url configured, send SETSERVER|http://host:port");
        return;
    }

    JsonDocument reqDoc;
    reqDoc["model"] = currentModel;
    reqDoc["stream"] = true;
    JsonArray messages = reqDoc["messages"].to<JsonArray>();
    JsonObject msg = messages.add<JsonObject>();
    msg["role"] = "user";
    msg["content"] = text;

    String body;
    serializeJson(reqDoc, body);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.begin(serverUrl + "/v1/chat/completions");
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(body);
    if(code != 200) {
        sendLine("ERR|chat request failed, http " + String(code));
        http.end();
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    String lineBuf;
    unsigned long lastData = millis();

    while(http.connected() && (millis() - lastData) < HTTP_TIMEOUT_MS) {
        while(stream->available()) {
            char c = stream->read();
            lastData = millis();
            if(c == '\n') {
                if(lineBuf.startsWith("data: ")) {
                    String payload = lineBuf.substring(6);
                    payload.trim();
                    if(payload == "[DONE]") {
                        sendLine("DONE");
                        http.end();
                        return;
                    }
                    JsonDocument chunk;
                    if(deserializeJson(chunk, payload) == DeserializationError::Ok) {
                        const char* delta =
                            chunk["choices"][0]["delta"]["content"] | (const char*)nullptr;
                        if(delta != nullptr && strlen(delta) > 0) {
                            sendLine("TOK|" + sanitizeForWire(String(delta)));
                        }
                    }
                }
                lineBuf = "";
            } else if(c != '\r') {
                lineBuf += c;
            }
        }
        delay(2);
    }
    sendLine("DONE");
    http.end();
}

static void handleLine(const String& line) {
    int sep = line.indexOf('|');
    String tag = sep >= 0 ? line.substring(0, sep) : line;
    String rest = sep >= 0 ? line.substring(sep + 1) : "";

    if(tag == "CHAT") {
        handleChat(rest);
    } else if(tag == "LISTMODELS") {
        handleListModels();
    } else if(tag == "SELMODEL") {
        currentModel = rest;
        sendLine("INFO|model set to " + currentModel);
    } else if(tag == "SETWIFI") {
        handleSetWifi(rest);
    } else if(tag == "SETSERVER") {
        handleSetServer(rest);
    } else if(tag == "REPROVISION") {
        handleReprovision();
    } else {
        sendLine("ERR|unknown command " + tag);
    }
}

void setup() {
    Serial.begin(AI_CHAT_UART_BAUD);
    delay(200);
    loadConfig();

    improvSerial.setDeviceInfo(
        ImprovTypes::ChipFamily::CF_ESP32_S2, "ai-chat-bridge", FIRMWARE_VERSION, "Flipper AI Chat Bridge");
    improvSerial.onImprovError(onImprovWiFiErrorCb);
    improvSerial.onImprovConnected(onImprovWiFiConnectedCb);

    // Try any previously-provisioned creds up front; if there are none (or
    // they fail), WiFi.status() stays disconnected and loop() falls into
    // the Improv branch below, ready for the web installer.
    connectWifiIfNeeded();
    if(WiFi.status() == WL_CONNECTED) {
        sendLine("INFO|ai-chat bridge booted, wifi up, ip " + WiFi.localIP().toString());
    }
}

void loop() {
    if(WiFi.status() != WL_CONNECTED) {
        // No WiFi yet: hand the port entirely to Improv so the web
        // installer can provision it. See the comment above
        // onImprovWiFiErrorCb() for why these two can't interleave.
        improvSerial.handleSerial();
        return;
    }

    static String lineBuf;
    while(Serial.available()) {
        char c = Serial.read();
        if(c == '\n') {
            lineBuf.trim();
            if(lineBuf.length() > 0) handleLine(lineBuf);
            lineBuf = "";
        } else if(c != '\r') {
            lineBuf += c;
        }
    }
}
