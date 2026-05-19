#include <ESP8266WiFi.h>


#define BAUD_RATE 115200
#define SERIAL_TIMEOUT_MS 100

enum BridgeMode {
  MODE_AUTO_DETECT,
  MODE_AT_PASSTHROUGH,
  MODE_DEAUTHER_COMMANDS,
  MODE_RAW_COMMANDS
};

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

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.length() > 0) {
      processCommand(cmd);
    }
  }

  delay(10);
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
    else if (currentMode == MODE_DEAUTHER_COMMANDS) forwardToDeauther(cmd);
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

  if (response.indexOf("deauther") >= 0 ||
    response.indexOf("scan all") >= 0 ||
    response.indexOf("attack") >= 0) {
    currentMode = MODE_DEAUTHER_COMMANDS;
    Serial.println("[BRIDGE] Detected Deauther firmware");
    Serial.println("[BRIDGE] Switching to Deauther command mode");
  } else if (response.indexOf("ok") >= 0 ||
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

void forwardToDeauther(String cmd) {

  String translated = translateToDeauther(cmd);

  if (translated.length() > 0) {
    Serial.println(translated);
    delay(200);

    unsigned long timeout = millis() + 10000;
    bool commandDone = false;

    while (millis() < timeout && !commandDone) {
      while (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        if (line.length() > 0) {
          Serial.println(line);

          line.toLowerCase();
          if (line.indexOf("done") >= 0 ||
            line.indexOf("error") >= 0 ||
            line.indexOf("ok") >= 0) {
            commandDone = true;
            break;
          }
        }
      }
      delay(10);
    }
  }
}

String translateToDeauther(String cmd) {
  String lowerCmd = cmd;
  lowerCmd.toLowerCase();

  cmd.replace("\"", "");
  cmd.trim();

  if (lowerCmd == "at" || lowerCmd == "ping") {
    return "sysinfo";
  } else if (lowerCmd == "scan" || lowerCmd == "wifi scan") {
    return "scan all";
  } else if (lowerCmd.startsWith("join")) {

    int firstSpace = cmd.indexOf(' ');
    if (firstSpace > 0) {
      String rest = cmd.substring(firstSpace + 1);
      int secondSpace = rest.indexOf(' ');

      if (secondSpace > 0) {
        wifi_ssid = rest.substring(0, secondSpace);
        wifi_password = rest.substring(secondSpace + 1);

        Serial.print("[BRIDGE] Stored SSID: ");
        Serial.println(wifi_ssid);

        return "add ssid \"" + wifi_ssid + "\"";
      } else {
        wifi_ssid = rest;
        wifi_password = "";
        return "add ssid \"" + wifi_ssid + "\"";
      }
    }
    return "scan all";
  } else if (lowerCmd == "ip" || lowerCmd == "wifi ip") {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[BRIDGE] IP Address: ");
      Serial.println(WiFi.localIP());
      return "";
    } else {
      return "sysinfo";
    }
  } else if (lowerCmd == "status" || lowerCmd == "wifi status") {
    return "info";
  } else if (lowerCmd == "connect") {
    if (wifi_ssid.length() > 0) {
      return "@connect";
    }
    return "";
  } else {

    return cmd;
  }
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
  } else if (cmd == "@mode deauther") {
    currentMode = MODE_DEAUTHER_COMMANDS;
    Serial.println("[BRIDGE] Mode: Deauther Commands");
    Serial.println("[BRIDGE] Translating DeckOS wifi commands to Deauther");
  } else if (cmd == "@mode raw") {
    currentMode = MODE_RAW_COMMANDS;
    Serial.println("[BRIDGE] Mode: Raw Commands");
    Serial.println("[BRIDGE] Passing commands without translation");
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
    case MODE_DEAUTHER_COMMANDS:
      Serial.println("Deauther");
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