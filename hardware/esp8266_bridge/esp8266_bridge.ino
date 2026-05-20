#include <ESP8266WiFi.h>


#define BAUD_RATE 115200
#define SERIAL_TIMEOUT_MS 100

enum BridgeMode {
  MODE_AUTO_DETECT,
  MODE_AT_PASSTHROUGH,
  MODE_RAW_COMMANDS
};

WiFiServer httpServer(80);
bool httpServerRunning = false;

WiFiServer telnetServer(23);
bool telnetRunning = false;
WiFiClient telnetClient;

bool telnetPassthroughActive = false;

BridgeMode currentMode = MODE_AUTO_DETECT;

String wifi_ssid = "deckowifi";
String wifi_password = "decko";

String responseBuffer = "";

void setup() {
  Serial.begin(BAUD_RATE);
  Serial.setTimeout(SERIAL_TIMEOUT_MS);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  delay(100);
  Serial.println();
  Serial.println("[DECKOS_BRIDGE] ESP8266 Bridge v1.0");
  Serial.println("[DECKOS_BRIDGE] Mode: Auto-detect");
  Serial.println("[DECKOS_BRIDGE] Ready for commands");

  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
  }
}

void loop() {
  handleHttpServer();
  handleTelnetServer();

  if (telnetPassthroughActive) {
    forwardPicoOutputToTelnet();

    // After forwarding output, if telnet client is connected and
    // Serial has been quiet for a moment, send the prompt
    static uint32_t lastSerialActivity = 0;
    static bool promptPending = false;

    if (Serial.available()) {
      lastSerialActivity = millis();
      promptPending = true;
    }
    if (promptPending && (millis() - lastSerialActivity) > 150) {
      if (telnetClient && telnetClient.connected()) {
        telnetClient.print("> ");
      }
      promptPending = false;
    }
    return;
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      processCommand(cmd);
    }
  }

  delay(10);
}

void handleTelnetServer() {
  if (!telnetRunning) return;

  // Accept new client if none connected
  if (!telnetClient || !telnetClient.connected()) {
    // If client just dropped, clean up passthrough state
    if (telnetPassthroughActive) {
      telnetPassthroughActive = false;
      currentMode = MODE_RAW_COMMANDS;
    }

    WiFiClient newClient = telnetServer.available();
    if (newClient) {
      if (telnetClient) telnetClient.stop();
      telnetClient = newClient;
      telnetPassthroughActive = true;  // activate NOW when client connects
      currentMode = MODE_RAW_COMMANDS; // ensure bridge is in known good state
      telnetClient.println("[DeckOS] Connected. Type EXIT to disconnect.");
      telnetClient.print("> ");

    }
    return;
  }

  static String telnetLineBuffer = "";

  while (telnetClient.available()) {
    char c = (char)telnetClient.read();

    if (c == '\r') continue;  // ignore carriage return

    if (c == '\n') {
      telnetLineBuffer.trim();

      if (telnetLineBuffer == "exit" || telnetLineBuffer == "quit" ||
          telnetLineBuffer == "EXIT" || telnetLineBuffer == "QUIT") {
        telnetClient.println("[DeckOS] Disconnecting.");
        telnetClient.stop();
        telnetPassthroughActive = false;
        currentMode = MODE_RAW_COMMANDS;
        telnetLineBuffer = "";
        return;
      }

      if (telnetLineBuffer.length() > 0) {
        // Send command to Pico over Serial
        Serial.println(telnetLineBuffer);
      }

      telnetLineBuffer = "";
    } else {
      if (telnetLineBuffer.length() < 127) {
        telnetLineBuffer += c;
      }
    }
  }
}

void forwardPicoOutputToTelnet() {
  if (!telnetRunning) return;
  if (!telnetClient || !telnetClient.connected()) {
    telnetPassthroughActive = false;
    currentMode = MODE_RAW_COMMANDS;
    return;
  }

  // Forward Pico output to telnet client, char by char, non-blocking
  static String stopCheckBuf = "";

  while (Serial.available()) {
    char c = (char)Serial.read();

    // Check for @stoptelnet escape sequence
    stopCheckBuf += c;
    if (stopCheckBuf.length() > 12)
      stopCheckBuf = stopCheckBuf.substring(stopCheckBuf.length() - 12);

    if (stopCheckBuf.indexOf("@stoptelnet") >= 0) {
      stopCheckBuf = "";
      telnetPassthroughActive = false;
      currentMode = MODE_RAW_COMMANDS;
      if (telnetClient) {
        telnetClient.println("\r\n[DeckOS] Telnet server stopped.");
        telnetClient.stop();
      }
      telnetServer.stop();
      telnetRunning = false;
      return;
    }

    telnetClient.write(c);
  }
}

void handleHttpServer() {
  if (!httpServerRunning) return;

  WiFiClient client = httpServer.available();
  if (!client) return;

  unsigned long timeout = millis() + 3000;
  while (!client.available() && millis() < timeout) {
    delay(1);
  }
  if (!client.available()) { client.stop(); return; }

  String requestLine = client.readStringUntil('\n');
  requestLine.trim();

  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
  }

  String method = "";
  String path   = "/";
  int s1 = requestLine.indexOf(' ');
  int s2 = requestLine.lastIndexOf(' ');
  if (s1 > 0 && s2 > s1) {
    method = requestLine.substring(0, s1);
    path   = requestLine.substring(s1 + 1, s2);
  }

  Serial.print("[HTTP] ");
  Serial.print(method);
  Serial.print(" ");
  Serial.println(path);

  String body = "";

  if (path == "/" || path == "/status") {
    body = "{\"status\":\"ok\",\"ssid\":\"" + WiFi.SSID() +
           "\",\"ip\":\"" + WiFi.localIP().toString() +
           "\",\"rssi\":" + String(WiFi.RSSI()) + "}";
  } else if (path == "/scan") {
    int n = WiFi.scanNetworks();
    body = "{\"networks\":[";
    for (int i = 0; i < n; i++) {
      if (i > 0) body += ",";
      body += "{\"ssid\":\"" + WiFi.SSID(i) +
              "\",\"rssi\":" + String(WiFi.RSSI(i)) +
              ",\"secure\":" + (WiFi.encryptionType(i) == ENC_TYPE_NONE ? "false" : "true") + "}";
    }
    body += "]}";
    WiFi.scanDelete();
  } else if (path == "/reset") {
    body = "{\"status\":\"rebooting\"}";
    client.print("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
    client.print(body.length());
    client.print("\r\n\r\n");
    client.print(body);
    client.stop();
    delay(200);
    ESP.restart();
    return;
  } else {
    body = "{\"error\":\"not found\",\"path\":\"" + path + "\"}";
    client.print("HTTP/1.0 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: ");
    client.print(body.length());
    client.print("\r\n\r\n");
    client.print(body);
    client.stop();
    return;
  }

  client.print("HTTP/1.0 200 OK\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: ");
  client.print(body.length());
  client.print("\r\n\r\n");
  client.print(body);
  client.stop();
}

void processCommand(String cmd) {
    if (cmd.startsWith("@")) {
        handleBridgeCommand(cmd);
        return;
    }

    if (cmd.startsWith("join ")) {
        int firstSpace = cmd.indexOf(' ');
        String rest = cmd.substring(firstSpace + 1);
        int secondSpace = rest.indexOf(' ');
        if (secondSpace > 0) {
            wifi_ssid = rest.substring(0, secondSpace);
            wifi_password = rest.substring(secondSpace + 1);
        } else {
            wifi_ssid = rest;
            wifi_password = "";
        }
        Serial.print("[BRIDGE] Credentials stored. SSID: ");
        Serial.println(wifi_ssid);
        Serial.println("OK");
        return;
    }

    if (currentMode == MODE_AUTO_DETECT)         autoDetectAndRoute(cmd);
    else if (currentMode == MODE_AT_PASSTHROUGH) forwardToATFirmware(cmd);
    else if (currentMode == MODE_RAW_COMMANDS)   forwardRaw(cmd);
}

void autoDetectAndRoute(String cmd) {

  Serial.println(cmd);
  delay(300);

  String response = "";
  unsigned long timeout = millis() + 2000;
  while (millis() < timeout) {
    while (Serial.available()) {
      response += (char) Serial.read();
    }
    delay(10);
  }

  response.toLowerCase();

 
  if (response.indexOf("ok") >= 0 ||
    response.indexOf("ready") >= 0 ||
    cmd == "AT") {
    currentMode = MODE_AT_PASSTHROUGH;
    Serial.println("[BRIDGE] Detected AT firmware");
    Serial.println("[BRIDGE] Switching to AT passthrough mode");
  } else {
    currentMode = MODE_RAW_COMMANDS;
    Serial.println("[BRIDGE] Unknown firmware detected");
    Serial.println("[BRIDGE] Switching to raw command mode");
  }

  processCommand(cmd);
}

void forwardToATFirmware(String cmd) {
  Serial.println(cmd);
  delay(100);

  unsigned long timeout = millis() + 5000;
  while (millis() < timeout) {
    while (Serial.available()) {
      char c = Serial.read();
      Serial.write(c);
    }
    delay(10);
  }
  Serial.println();
}


void forwardRaw(String cmd) {

  Serial.println(cmd);

  unsigned long timeout = millis() + 5000;
  while (millis() < timeout) {
    while (Serial.available()) {
      Serial.write(Serial.read());
    }
    delay(10);
  }
  Serial.println();
}

void handleBridgeCommand(String cmd) {
  if (cmd == "@mode auto") {
    currentMode = MODE_AUTO_DETECT;
    Serial.println("[BRIDGE] Mode: Auto-detect");
  } else if (cmd == "@mode at") {
    currentMode = MODE_AT_PASSTHROUGH;
    Serial.println("[BRIDGE] Mode: AT Passthrough");
    Serial.println("[BRIDGE] Sending AT commands directly");

  } else if (cmd == "@mode raw") {
    currentMode = MODE_RAW_COMMANDS;
    Serial.println("[BRIDGE] Mode: Raw Commands");
    Serial.println("[BRIDGE] Passing commands without translation");
  } else if (cmd == "@serve") {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[BRIDGE] Not connected to WiFi - run @connect first");
    return;
  }
  if (!httpServerRunning) {
    httpServer.begin();
    httpServerRunning = true;
  }
  Serial.print("[BRIDGE] HTTP server started on http://");
  Serial.print(WiFi.localIP());
  Serial.println(":80");
  Serial.println("[BRIDGE] Endpoints: / /status /scan /reset");
} else if (cmd == "@telnet") {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[BRIDGE] Not connected to WiFi");
    return;
  }
  if (!telnetRunning) {
    telnetServer.begin();
    telnetRunning = true;
  }
  telnetPassthroughActive = true;
  Serial.println("[TELNET] Server started on port 23");
  Serial.print("[TELNET] Connect to: telnet ");
  Serial.println(WiFi.localIP());


} else if (cmd == "@stoptelnet") {
  telnetPassthroughActive = false;
  if (telnetClient) telnetClient.stop();
  telnetServer.stop();
  telnetRunning = false;
  Serial.println("[TELNET] Stopped");
} else if (cmd == "@stopserve") {
  httpServer.stop();
  httpServerRunning = false;
  Serial.println("[BRIDGE] HTTP server stopped");
  } else if (cmd.startsWith("@get ")) {
  String url = cmd.substring(5);
  url.trim();

  // Parse host and path from "host/path" or "http://host/path"
  if (url.startsWith("http://")) url = url.substring(7);
  int slash = url.indexOf('/');
  String host = (slash > 0) ? url.substring(0, slash) : url;
  String path = (slash > 0) ? url.substring(slash)    : "/";

  // Parse optional port
  uint16_t port = 80;
  int colon = host.indexOf(':');
  if (colon > 0) {
    port = host.substring(colon + 1).toInt();
    host = host.substring(0, colon);
  }

  Serial.print("[BRIDGE] GET http://");
  Serial.print(host);
  Serial.println(path);

  WiFiClient client;
  if (!client.connect(host.c_str(), port)) {
    Serial.println("[BRIDGE] Connection failed");
    return;
  }

  client.print("GET " + path + " HTTP/1.0\r\n");
  client.print("Host: " + host + "\r\n");
  client.print("Connection: close\r\n\r\n");

  // Wait for response
  unsigned long timeout = millis() + 10000;
  while (!client.available() && millis() < timeout) delay(10);

  // Skip headers, print body
  bool pastHeaders = false;
  String headerLine = "";
  while (client.available()) {
    char c = client.read();
    if (!pastHeaders) {
      headerLine += c;
      if (headerLine.endsWith("\r\n\r\n")) {
        pastHeaders = true;
        headerLine = "";
      }
    } else {
      Serial.write(c);
    }
  }
  Serial.println();
  Serial.println("[BRIDGE] GET done");
  client.stop();

} else if (cmd.startsWith("@post ")) {
  // Format: @post host/path body_here
  String rest = cmd.substring(6);
  int space = rest.indexOf(' ');
  String url  = (space > 0) ? rest.substring(0, space) : rest;
  String body = (space > 0) ? rest.substring(space + 1) : "";

  if (url.startsWith("http://")) url = url.substring(7);
  int slash = url.indexOf('/');
  String host = (slash > 0) ? url.substring(0, slash) : url;
  String path = (slash > 0) ? url.substring(slash)    : "/";

  uint16_t port = 80;
  int colon = host.indexOf(':');
  if (colon > 0) {
    port = host.substring(colon + 1).toInt();
    host = host.substring(0, colon);
  }

  Serial.print("[BRIDGE] POST http://");
  Serial.print(host);
  Serial.println(path);

  WiFiClient client;
  if (!client.connect(host.c_str(), port)) {
    Serial.println("[BRIDGE] Connection failed");
    return;
  }

  client.print("POST " + path + " HTTP/1.0\r\n");
  client.print("Host: " + host + "\r\n");
  client.print("Content-Type: application/x-www-form-urlencoded\r\n");
  client.print("Content-Length: " + String(body.length()) + "\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(body);

  unsigned long timeout = millis() + 10000;
  while (!client.available() && millis() < timeout) delay(10);

  bool pastHeaders = false;
  String headerLine = "";
  while (client.available()) {
    char c = client.read();
    if (!pastHeaders) {
      headerLine += c;
      if (headerLine.endsWith("\r\n\r\n")) { pastHeaders = true; }
    } else {
      Serial.write(c);
    }
  }
  Serial.println();
  Serial.println("[BRIDGE] POST done");
  client.stop();
  } else if (cmd == "@status") {
    Serial.println("[BRIDGE] ====================");
    Serial.print("[BRIDGE] Mode: ");
    switch (currentMode) {
    case MODE_AUTO_DETECT:
      Serial.println("Auto-detect");
      break;
    case MODE_AT_PASSTHROUGH:
      Serial.println("AT Passthrough");
      break;
    case MODE_RAW_COMMANDS:
      Serial.println("Raw");
      break;
    }
    Serial.print("[BRIDGE] WiFi: ");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Connected to ");
      Serial.print(WiFi.SSID());
      Serial.print(" (");
      Serial.print(WiFi.RSSI());
      Serial.print(" dBm) IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Disconnected");
    }
    Serial.print("[BRIDGE] LED: ");
    Serial.println(digitalRead(LED_BUILTIN) ? "OFF" : "ON");
    Serial.println("[BRIDGE] ====================");
  } else if (cmd == "@reset") {
    Serial.println("[BRIDGE] Resetting ESP8266...");
    delay(100);
    ESP.restart();
  } else if (cmd == "@connect") {
    if (wifi_ssid.length() > 0) {
      Serial.print("[BRIDGE] Connecting to ");
      Serial.println(wifi_ssid);

      WiFi.mode(WIFI_STA);
      WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;

        if (attempts % 10 == 0) {
          Serial.println();
          Serial.print("[BRIDGE] Still trying... ");
        }
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("[BRIDGE] ✓ Connected successfully!");
        Serial.print("[BRIDGE] IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("[BRIDGE] Signal strength: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
      } else {
        Serial.println();
        Serial.println("[BRIDGE] ✗ Connection failed");
        Serial.println("[BRIDGE] Check SSID/password and signal strength");
      }
    } else {
      Serial.println("[BRIDGE] No SSID configured");
      Serial.println("[BRIDGE] Use: join YOUR_SSID YOUR_PASSWORD");
    }
  } else if (cmd == "@scan") {
    Serial.println("[BRIDGE] Scanning for WiFi networks...");
    int networks = WiFi.scanNetworks();

    if (networks == 0) {
      Serial.println("[BRIDGE] No networks found");
    } else {
      Serial.print("[BRIDGE] Found ");
      Serial.print(networks);
      Serial.println(" networks:");

      for (int i = 0; i < networks; i++) {
        Serial.print("  ");
        Serial.print(i + 1);
        Serial.print(". ");
        Serial.print(WiFi.SSID(i));
        Serial.print(" (");
        Serial.print(WiFi.RSSI(i));
        Serial.print(" dBm) ");
        Serial.println(WiFi.encryptionType(i) == ENC_TYPE_NONE ? "[OPEN]" : "[SECURED]");
      }
    }
    WiFi.scanDelete();
  } else {
    Serial.println("[BRIDGE] Unknown bridge command");
    Serial.println("[BRIDGE] Available: @mode, @status, @reset, @connect, @scan");
  }
}