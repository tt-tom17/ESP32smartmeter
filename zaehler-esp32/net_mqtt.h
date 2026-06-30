// ─────────────────────────────────────────────────────────────────────────────
//  net_mqtt.h — WLAN- + MQTT-Verbindung
//
//  Wird aus zaehler-esp32.ino NACH config.h und globals.h inkludiert.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);            // WiFi-Stromsparen aus -> OTA/UDP zuverlässig
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WLAN ok, MAC: "); Serial.print(WiFi.macAddress());
    Serial.print(", IP: ");         Serial.println(WiFi.localIP());
  }
}

void ensureMqtt() {
  if (!mqttEnabled || mqtt.connected() || WiFi.status() != WL_CONNECTED) return;

  // Reconnect höchstens alle 5 s versuchen (sonst blockiert ein toter Broker die loop()).
  static unsigned long lastTry = 0;
  unsigned long now = millis();
  if (lastTry != 0 && now - lastTry < 5000) return;
  lastTry = now;

  // Broker zuerst mit KURZEM Timeout anpingen. Ohne das würde PubSubClient.connect()
  // bei nicht erreichbarem Host ~30 s blockieren -> Task-Watchdog-Reset / Reboot-Loop.
  WiFiClient probe;
  if (!probe.connect(mqttServer.c_str(), mqttPort, 1500)) { probe.stop(); return; }
  probe.stop();

  String lwt = mqttRoot + "/online";
  bool ok;
  if (mqttUser.length())
    ok = mqtt.connect(MQTT_CLIENT_ID, mqttUser.c_str(), mqttPass.c_str(), lwt.c_str(), 0, true, "0");
  else
    ok = mqtt.connect(MQTT_CLIENT_ID, lwt.c_str(), 0, true, "0");   // anonym + LWT
  if (ok) mqtt.publish(lwt.c_str(), "1", true);
}

// MQTT-Konfig anwenden: immer trennen; bei aktivem MQTT Server setzen, dann
// verbindet ensureMqtt() neu. Bei deaktiviertem MQTT bleibt es getrennt.
void applyMqtt() {
  mqtt.disconnect();
  if (mqttEnabled) mqtt.setServer(mqttServer.c_str(), mqttPort);
}

// '.' und '*' -> '_'  =>  "6.26*01" -> "6_26_01"  (MQTT-tauglich)
String topicify(const String& code) {
  String t;
  for (char c : code) t += (c == '.' || c == '*') ? '_' : c;
  return t;
}
