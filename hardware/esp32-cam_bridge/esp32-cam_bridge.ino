#include <WiFi.h>
#include <esp_now.h>
#include <PubSubClient.h>

#define LED_PIN         33
#define LED_ON()        digitalWrite(LED_PIN, LOW)
#define LED_OFF()       digitalWrite(LED_PIN, HIGH)
#define LED_TOGGLE()    digitalWrite(LED_PIN, !digitalRead(LED_PIN))

#define BAUD_RATE         115200
#define SERIAL_TIMEOUT_MS 100

#define SWARM_CHANNEL   1
#define SWARM_MAX_PEERS 8

enum BridgeMode {
    MODE_AUTO_DETECT,
    MODE_AT_PASSTHROUGH,
    MODE_RAW_COMMANDS
};
BridgeMode currentMode = MODE_AUTO_DETECT;

String wifi_ssid     = "deckowifi";
String wifi_password = "decko";

WiFiClient   mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
String       mqtt_server    = "";
int          mqtt_port      = 1883;
String       mqtt_client_id = "deckos";

WiFiServer httpServer(80);
bool       httpServerRunning = false;

WiFiServer telnetServer(23);
bool       telnetRunning           = false;
WiFiClient telnetClient;
bool       telnetPassthroughActive = false;

typedef struct {
    char     node_id[16];
    float    lat, lon, alt, heading;
    uint8_t  state;
    uint32_t timestamp;
} swarm_packet_t;

static swarm_packet_t s_peers[SWARM_MAX_PEERS];
static int            s_peer_count    = 0;
static char           s_node_id[16]   = "drone1";
static bool           s_espnow_active = false;


static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len != (int)sizeof(swarm_packet_t)) return;
    swarm_packet_t pkt;
    memcpy(&pkt, data, sizeof(pkt));
    for (int i = 0; i < s_peer_count; i++) {
        if (strcmp(s_peers[i].node_id, pkt.node_id) == 0) {
            memcpy(&s_peers[i], &pkt, sizeof(pkt));
            Serial.printf("[SWARM] update %s lat=%.6f lon=%.6f alt=%.1f\n",
                          pkt.node_id, pkt.lat, pkt.lon, pkt.alt);
            return;
        }
    }
    if (s_peer_count < SWARM_MAX_PEERS) {
        memcpy(&s_peers[s_peer_count++], &pkt, sizeof(pkt));
        Serial.printf("[SWARM] new peer %s\n", pkt.node_id);
    }
}

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {}

void swarm_broadcast(swarm_packet_t *pkt) {
    uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_send(broadcast, (uint8_t *)pkt, sizeof(swarm_packet_t));
}

void processCommand(String cmd);
void handleBridgeCommand(String cmd);
void handleHttpServer();
void handleTelnetServer();
void forwardPicoOutputToTelnet();
void forwardToPassthrough(String cmd);
void forwardRaw(String cmd);
void autoDetectAndRoute(String cmd);

void setup() {
    Serial.begin(BAUD_RATE);
    Serial.setTimeout(SERIAL_TIMEOUT_MS);
    pinMode(LED_PIN, OUTPUT);
    LED_OFF();
    delay(100);
    Serial.println();
    Serial.println("[DECKOS_BRIDGE] ESP32-CAM Bridge v1.0");
    Serial.println("[DECKOS_BRIDGE] Mode: Auto-detect");
    Serial.println("[DECKOS_BRIDGE] Ready for commands");
    for (int i = 0; i < 3; i++) { LED_ON(); delay(100); LED_OFF(); delay(100); }
}

void loop() {
    handleHttpServer();
    handleTelnetServer();

    if (telnetPassthroughActive) {
        forwardPicoOutputToTelnet();
        static uint32_t lastSerialActivity = 0;
        static bool     promptPending      = false;
        if (Serial.available()) { lastSerialActivity = millis(); promptPending = true; }
        if (promptPending && (millis() - lastSerialActivity) > 150) {
            if (telnetClient && telnetClient.connected()) telnetClient.print("> ");
            promptPending = false;
        }
        return;
    }

    if (mqttClient.connected()) mqttClient.loop();

    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.length() > 0) processCommand(cmd);
    }
    delay(10);
}

void processCommand(String cmd) {
    if (cmd.startsWith("@")) { handleBridgeCommand(cmd); return; }

    if (cmd.startsWith("join ")) {
        String rest = cmd.substring(5);
        int sp = rest.indexOf(' ');
        if (sp > 0) { wifi_ssid = rest.substring(0, sp); wifi_password = rest.substring(sp + 1); }
        else        { wifi_ssid = rest; wifi_password = ""; }
        Serial.print("[BRIDGE] Credentials stored. SSID: ");
        Serial.println(wifi_ssid);
        Serial.println("OK");
        return;
    }

    if      (currentMode == MODE_AUTO_DETECT)    autoDetectAndRoute(cmd);
    else if (currentMode == MODE_AT_PASSTHROUGH) forwardToPassthrough(cmd);
    else                                         forwardRaw(cmd);
}

void autoDetectAndRoute(String cmd) {
    Serial.println(cmd);
    delay(300);
    String response = "";
    unsigned long t = millis() + 2000;
    while (millis() < t) { while (Serial.available()) response += (char)Serial.read(); delay(10); }
    response.toLowerCase();
    if (response.indexOf("ok") >= 0 || response.indexOf("ready") >= 0 || cmd == "AT") {
        currentMode = MODE_AT_PASSTHROUGH;
        Serial.println("[BRIDGE] Detected AT firmware -> AT passthrough mode");
    } else {
        currentMode = MODE_RAW_COMMANDS;
        Serial.println("[BRIDGE] Unknown firmware -> raw command mode");
    }
    processCommand(cmd);
}

void forwardToPassthrough(String cmd) {
    Serial.println(cmd); delay(100);
    unsigned long t = millis() + 5000;
    while (millis() < t) { while (Serial.available()) Serial.write(Serial.read()); delay(10); }
    Serial.println();
}

void forwardRaw(String cmd) {
    Serial.println(cmd);
    unsigned long t = millis() + 5000;
    while (millis() < t) { while (Serial.available()) Serial.write(Serial.read()); delay(10); }
    Serial.println();
}

void handleTelnetServer() {
    if (!telnetRunning) return;
    if (!telnetClient || !telnetClient.connected()) {
        if (telnetPassthroughActive) { telnetPassthroughActive = false; currentMode = MODE_RAW_COMMANDS; }
        WiFiClient nc = telnetServer.available();
        if (nc) {
            if (telnetClient) telnetClient.stop();
            telnetClient = nc;
            telnetPassthroughActive = true;
            currentMode = MODE_RAW_COMMANDS;
            telnetClient.println("[DeckOS] Connected. Type EXIT to disconnect.");
            telnetClient.print("> ");
        }
        return;
    }
    static String telnetLineBuf = "";
    while (telnetClient.available()) {
        char c = (char)telnetClient.read();
        if (c == '\r') continue;
        if (c == '\n') {
            telnetLineBuf.trim();
            if (telnetLineBuf == "exit" || telnetLineBuf == "EXIT" ||
                telnetLineBuf == "quit" || telnetLineBuf == "QUIT") {
                telnetClient.println("[DeckOS] Disconnecting.");
                telnetClient.stop();
                telnetPassthroughActive = false;
                currentMode = MODE_RAW_COMMANDS;
            } else if (telnetLineBuf.length() > 0) {
                Serial.println(telnetLineBuf);
            }
            telnetLineBuf = "";
        } else if (telnetLineBuf.length() < 127) {
            telnetLineBuf += c;
        }
    }
}

void forwardPicoOutputToTelnet() {
    if (!telnetClient || !telnetClient.connected()) {
        telnetPassthroughActive = false; currentMode = MODE_RAW_COMMANDS; return;
    }
    static String stopBuf = "";
    while (Serial.available()) {
        char c = (char)Serial.read();
        stopBuf += c;
        if (stopBuf.length() > 12) stopBuf = stopBuf.substring(stopBuf.length() - 12);
        if (stopBuf.indexOf("@stoptelnet") >= 0) {
            stopBuf = "";
            telnetPassthroughActive = false;
            currentMode = MODE_RAW_COMMANDS;
            if (telnetClient) { telnetClient.println("\r\n[DeckOS] Telnet server stopped."); telnetClient.stop(); }
            telnetServer.stop(); telnetRunning = false; return;
        }
        telnetClient.write(c);
    }
}

void handleHttpServer() {
    if (!httpServerRunning) return;
    WiFiClient client = httpServer.available();
    if (!client) return;
    unsigned long deadline = millis() + 3000;
    while (!client.available() && millis() < deadline) delay(1);
    if (!client.available()) { client.stop(); return; }
    String reqLine = client.readStringUntil('\n'); reqLine.trim();
    while (client.available()) { String l = client.readStringUntil('\n'); l.trim(); if (l.length() == 0) break; }
    String method, path = "/";
    int s1 = reqLine.indexOf(' '), s2 = reqLine.lastIndexOf(' ');
    if (s1 > 0 && s2 > s1) { method = reqLine.substring(0,s1); path = reqLine.substring(s1+1,s2); }
    String body; int code = 200;
    if (path == "/" || path == "/status") {
        body = "{\"status\":\"ok\",\"ssid\":\"" + WiFi.SSID() +
               "\",\"ip\":\"" + WiFi.localIP().toString() +
               "\",\"rssi\":" + String(WiFi.RSSI()) + "}";
    } else if (path == "/scan") {
        int n = WiFi.scanNetworks(); body = "{\"networks\":[";
        for (int i = 0; i < n; i++) {
            if (i) body += ",";
            body += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i) +
                    ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false":"true") + "}";
        }
        body += "]}"; WiFi.scanDelete();
    } else if (path == "/reset") {
        body = "{\"status\":\"rebooting\"}";
        client.printf("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n", body.length());
        client.print(body); client.stop(); delay(200); ESP.restart(); return;
    } else {
        code = 404;
        body = "{\"error\":\"not found\",\"path\":\"" + path + "\"}";
    }
    client.printf("HTTP/1.0 %d OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n", code, body.length());
    client.print(body); client.stop();
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
    Serial.print("[MQTT] "); Serial.print(topic); Serial.print(" ");
    for (unsigned int i = 0; i < length; i++) Serial.write(payload[i]);
    Serial.println();
}


void handleBridgeCommand(String cmd) {

    if (cmd == "@mode auto")  { currentMode = MODE_AUTO_DETECT;    Serial.println("[BRIDGE] Mode: Auto-detect");    return; }
    if (cmd == "@mode at")    { currentMode = MODE_AT_PASSTHROUGH; Serial.println("[BRIDGE] Mode: AT Passthrough"); return; }
    if (cmd == "@mode raw")   { currentMode = MODE_RAW_COMMANDS;   Serial.println("[BRIDGE] Mode: Raw");            return; }

    if (cmd == "@status") {
        Serial.println("[BRIDGE] ====================");
        Serial.print("[BRIDGE] Mode: ");
        if      (currentMode == MODE_AUTO_DETECT)    Serial.println("Auto-detect");
        else if (currentMode == MODE_AT_PASSTHROUGH) Serial.println("AT Passthrough");
        else                                         Serial.println("Raw");
        Serial.print("[BRIDGE] WiFi: ");
        if (WiFi.status() == WL_CONNECTED)
            Serial.printf("Connected to %s (%d dBm) IP: %s\n",
                          WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str());
        else
            Serial.println("Disconnected");
        Serial.println("[BRIDGE] ====================");
        return;
    }

    if (cmd == "@reset") { Serial.println("[BRIDGE] Resetting..."); delay(100); ESP.restart(); return; }

    if (cmd == "@connect") {
        if (wifi_ssid.length() == 0) { Serial.println("[BRIDGE] No SSID. Use: join SSID PASSWORD"); return; }
        Serial.print("[BRIDGE] Connecting to "); Serial.println(wifi_ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 40) {
            delay(500); Serial.print(".");
            if (++attempts % 10 == 0) Serial.println();
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println();
            Serial.println("[BRIDGE] Connected!");
            Serial.printf("[BRIDGE] IP: %s  RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        } else {
            Serial.println();
            Serial.println("[BRIDGE] Connection failed. Check SSID/password.");
        }
        return;
    }

    if (cmd == "@scan") {
        Serial.println("[BRIDGE] Scanning...");
        int n = WiFi.scanNetworks();
        if (n == 0) { Serial.println("[BRIDGE] No networks found"); }
        else {
            Serial.printf("[BRIDGE] Found %d networks:\n", n);
            for (int i = 0; i < n; i++)
                Serial.printf("  %d. %s (%d dBm) %s\n", i+1,
                              WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                              WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "[OPEN]" : "[SECURED]");
        }
        WiFi.scanDelete();
        return;
    }

    if (cmd == "@serve") {
        if (WiFi.status() != WL_CONNECTED) { Serial.println("[BRIDGE] Not connected - run @connect first"); return; }
        if (!httpServerRunning) { httpServer.begin(); httpServerRunning = true; }
        
        Serial.printf("[BRIDGE] HTTP server on http:
        Serial.println("[BRIDGE] Endpoints: / /status /scan /reset");
        return;
    }
    if (cmd == "@stopserve") { httpServer.stop(); httpServerRunning = false; Serial.println("[BRIDGE] HTTP stopped"); return; }

    if (cmd == "@telnet") {
        if (WiFi.status() != WL_CONNECTED) { Serial.println("[BRIDGE] Not connected"); return; }
        if (!telnetRunning) { telnetServer.begin(); telnetRunning = true; }
        telnetPassthroughActive = true;
        Serial.println("[TELNET] Server started on port 23");
        Serial.printf("[TELNET] Connect: telnet %s\n", WiFi.localIP().toString().c_str());
        return;
    }
    if (cmd == "@stoptelnet") {
        telnetPassthroughActive = false;
        if (telnetClient) telnetClient.stop();
        telnetServer.stop(); telnetRunning = false;
        Serial.println("[TELNET] Stopped");
        return;
    }

    if (cmd.startsWith("@get ")) {
        String url = cmd.substring(5); url.trim();
        
        if (url.startsWith("http:
        int slash = url.indexOf('/');
        String host = (slash > 0) ? url.substring(0, slash) : url;
        String path = (slash > 0) ? url.substring(slash) : "/";
        uint16_t port = 80;
        int colon = host.indexOf(':');
        if (colon > 0) { port = host.substring(colon+1).toInt(); host = host.substring(0, colon); }
        Serial.printf("[BRIDGE] GET http:
        WiFiClient client;
        if (!client.connect(host.c_str(), port)) { Serial.println("[BRIDGE] Connection failed"); return; }
        client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path.c_str(), host.c_str());
        unsigned long t = millis() + 10000;
        while (!client.available() && millis() < t) delay(10);
        bool past = false; String hbuf;
        while (client.available()) {
            char c = client.read();
            if (!past) { hbuf += c; if (hbuf.endsWith("\r\n\r\n")) past = true; }
            else Serial.write(c);
        }
        Serial.println(); Serial.println("[BRIDGE] GET done"); client.stop();
        return;
    }

    if (cmd.startsWith("@post ")) {
        String rest = cmd.substring(6);
        int sp = rest.indexOf(' ');
        String url  = (sp > 0) ? rest.substring(0, sp) : rest;
        String body = (sp > 0) ? rest.substring(sp+1) : "";
        
        if (url.startsWith("http:
        int slash = url.indexOf('/');
        String host = (slash > 0) ? url.substring(0, slash) : url;
        String path = (slash > 0) ? url.substring(slash) : "/";
        uint16_t port = 80;
        int colon = host.indexOf(':');
        if (colon > 0) { port = host.substring(colon+1).toInt(); host = host.substring(0, colon); }
        WiFiClient client;
        if (!client.connect(host.c_str(), port)) { Serial.println("[BRIDGE] Connection failed"); return; }
        client.printf("POST %s HTTP/1.0\r\nHost: %s\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                      path.c_str(), host.c_str(), body.length(), body.c_str());
        unsigned long t = millis() + 10000;
        while (!client.available() && millis() < t) delay(10);
        bool past = false; String hbuf;
        while (client.available()) {
            char c = client.read();
            if (!past) { hbuf += c; if (hbuf.endsWith("\r\n\r\n")) past = true; }
            else Serial.write(c);
        }
        Serial.println(); Serial.println("[BRIDGE] POST done"); client.stop();
        return;
    }

    if (cmd.startsWith("@mqtt server "))  { mqtt_server = cmd.substring(13); mqtt_server.trim(); mqttClient.setServer(mqtt_server.c_str(), mqtt_port); Serial.println("[MQTT] server=" + mqtt_server); return; }
    if (cmd.startsWith("@mqtt port "))    { mqtt_port = cmd.substring(11).toInt(); mqttClient.setServer(mqtt_server.c_str(), mqtt_port); Serial.printf("[MQTT] port=%d\n", mqtt_port); return; }
    if (cmd.startsWith("@mqtt id "))      { mqtt_client_id = cmd.substring(9); mqtt_client_id.trim(); Serial.println("[MQTT] id=" + mqtt_client_id); return; }
    if (cmd == "@mqtt connect") {
        if (mqtt_server.length() == 0) { Serial.println("[MQTT] no server set"); return; }
        if (WiFi.status() != WL_CONNECTED) { Serial.println("[MQTT] wifi not connected"); return; }
        mqttClient.setCallback(mqttCallback);
        if (mqttClient.connect(mqtt_client_id.c_str())) Serial.println("[MQTT] connected");
        else Serial.printf("[MQTT] failed rc=%d\n", mqttClient.state());
        return;
    }
    if (cmd == "@mqtt disconnect") { mqttClient.disconnect(); Serial.println("[MQTT] disconnected"); return; }
    if (cmd == "@mqtt status") {
        Serial.println("[MQTT] server=" + mqtt_server);
        Serial.printf("[MQTT] port=%d id=%s connected=%s\n", mqtt_port, mqtt_client_id.c_str(), mqttClient.connected() ? "yes":"no");
        return;
    }
    if (cmd.startsWith("@mqtt pub ")) {
        String rest = cmd.substring(10); int sp = rest.indexOf(' ');
        if (sp < 0) { Serial.println("[MQTT] usage: @mqtt pub <topic> <msg>"); return; }
        if (!mqttClient.connected()) { Serial.println("[MQTT] not connected"); return; }
        mqttClient.publish(rest.substring(0,sp).c_str(), rest.substring(sp+1).c_str());
        Serial.println("[MQTT] published to " + rest.substring(0,sp));
        return;
    }
    if (cmd.startsWith("@mqtt sub "))   { String t=cmd.substring(10); t.trim(); if (!mqttClient.connected()){Serial.println("[MQTT] not connected");return;} mqttClient.subscribe(t.c_str()); Serial.println("[MQTT] subscribed to "+t); return; }
    if (cmd.startsWith("@mqtt unsub ")) { String t=cmd.substring(12); t.trim(); if (!mqttClient.connected()){Serial.println("[MQTT] not connected");return;} mqttClient.unsubscribe(t.c_str()); Serial.println("[MQTT] unsubscribed from "+t); return; }

    if (cmd == "@swarm init") {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        if (esp_now_init() != ESP_OK) { Serial.println("[SWARM] init failed"); return; }
        esp_now_register_recv_cb(espnow_recv_cb);
        esp_now_register_send_cb(espnow_send_cb);
        s_espnow_active = true;
        Serial.println("[SWARM] ready  MAC=" + WiFi.macAddress());
        return;
    }
    if (cmd.startsWith("@swarm id "))   { String id=cmd.substring(10); id.trim(); strncpy(s_node_id, id.c_str(), 15); Serial.println("[SWARM] id=" + String(s_node_id)); return; }
    if (cmd.startsWith("@swarm peer ")) {
        String mac_str = cmd.substring(12); mac_str.trim();
        esp_now_peer_info_t peer_info = {};
        peer_info.channel = SWARM_CHANNEL;
        peer_info.encrypt = false;
        sscanf(mac_str.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &peer_info.peer_addr[0],&peer_info.peer_addr[1],&peer_info.peer_addr[2],
               &peer_info.peer_addr[3],&peer_info.peer_addr[4],&peer_info.peer_addr[5]);
        esp_now_add_peer(&peer_info);
        Serial.println("[SWARM] peer added " + mac_str);
        return;
    }
    if (cmd.startsWith("@swarm pub ")) {
        if (!s_espnow_active) { Serial.println("[SWARM] not initialised"); return; }
        String rest = cmd.substring(11);
        swarm_packet_t pkt; memset(&pkt, 0, sizeof(pkt));
        strncpy(pkt.node_id, s_node_id, 15);
        pkt.timestamp = millis();
        sscanf(rest.c_str(), "%f %f %f %f %hhu", &pkt.lat,&pkt.lon,&pkt.alt,&pkt.heading,&pkt.state);
        swarm_broadcast(&pkt);
        Serial.println("[SWARM] broadcast sent");
        return;
    }
    if (cmd == "@swarm list") {
        Serial.printf("[SWARM] peers: %d\n", s_peer_count);
        for (int i = 0; i < s_peer_count; i++)
            Serial.printf("  %s lat=%.6f lon=%.6f alt=%.1f hdg=%.1f state=%d\n",
                          s_peers[i].node_id,s_peers[i].lat,s_peers[i].lon,
                          s_peers[i].alt,s_peers[i].heading,s_peers[i].state);
        return;
    }
    if (cmd == "@swarm mac")  { Serial.println(WiFi.macAddress()); return; }
    if (cmd == "@swarm stop") { esp_now_deinit(); s_espnow_active=false; s_peer_count=0; Serial.println("[SWARM] stopped"); return; }

    Serial.println("[BRIDGE] Unknown command");
    Serial.println("[BRIDGE] Available: @mode, @status, @reset, @connect, @scan,");
    Serial.println("[BRIDGE]            @serve, @telnet, @get, @post, @mqtt, @swarm");
}
