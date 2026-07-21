// ─────────────────────────────────────────────────────────────────────────────
//  net_mqtt.h — WLAN- + MQTT-Verbindung
//
//  Wird aus zaehler-esp32.ino NACH config.h und globals.h inkludiert.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

// ─── WLAN-Zugangsdaten im NVS (Namespace "zaehler", prefs ist in setup() offen) ─
void loadWifiCreds()  { wifiSsid = prefs.getString("wifi_ssid", "");
                        wifiPass = prefs.getString("wifi_pass", ""); }
void saveWifiCreds(const String& s, const String& p) { prefs.putString("wifi_ssid", s);
                                                       prefs.putString("wifi_pass", p); }
void clearWifiCreds() { prefs.remove("wifi_ssid"); prefs.remove("wifi_pass"); }

// Setup-Portal öffnen: offener SoftAP + Captive-DNS. WIFI_AP_STA, damit im Portal
// ein WLAN-Scan (STA-Interface) möglich ist; es wird KEIN WiFi.begin() aufgerufen.
// Nur aus setup()/loop() aufrufen — nie aus einem Web-Handler (Thread-Safety).
void startProvisioningAP() {
  if (apMode) return;
  apMode = true;
  apStartedAt = millis();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID);                       // offen (kein Passwort)
  dnsServer.setTTL(3600);
  dnsServer.start(53, "*", apIP);             // Wildcard -> alles auf 192.168.4.1
  Serial.printf("[AP] Setup-Portal '%s' offen @ %s\n",
                AP_SSID, WiFi.softAPIP().toString().c_str());
}

// WLAN-Zustandsautomat:
//   apMode          -> nur den Captive-DNS bedienen
//   verbunden       -> fertig
//   keine Creds     -> Setup-Portal öffnen
//   Connect scheitert BEIM ERSTEN MAL -> Setup-Portal (Fallback)
//   Connect scheitert SPÄTER (WLAN-Blip) -> nur weiter versuchen, kein Portal
void ensureWifi() {
  if (apMode) { dnsServer.processNextRequest(); return; }
  static bool everConnected = false;
  if (WiFi.status() == WL_CONNECTED) { everConnected = true; return; }

  if (wifiSsid.isEmpty()) { startProvisioningAP(); return; }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);            // WiFi-Stromsparen aus -> OTA/UDP zuverlässig
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  unsigned long t = millis();
  // Busy-Wait bis ~15 s: den Task-Watchdog dabei füttern (loopTask lebt, wartet nur).
  while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_CONNECT_TIMEOUT_MS) { delay(250); esp_task_wdt_reset(); }
  if (WiFi.status() == WL_CONNECTED) {
    everConnected = true;
    Serial.print("WLAN ok, MAC: "); Serial.print(WiFi.macAddress());
    Serial.print(", IP: ");         Serial.println(WiFi.localIP());
  } else if (!everConnected) {
    Serial.println("[WiFi] Erstverbindung fehlgeschlagen -> Setup-Portal");
    startProvisioningAP();          // nur beim ersten Boot ins Portal fallen
  }
}

// NTP starten: setzt TZ (DE, inkl. DST) und die Zeitserver. Läuft asynchron im
// Hintergrund weiter und synchronisiert, sobald STA verbunden ist. Mehrfachaufruf
// (z.B. nach Reconnect) ist unschädlich. Ohne Internet bleibt time() < TIME_VALID.
void startTime() {
  configTzTime(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
  Serial.println("[NTP] Zeitsync gestartet (TZ " TZ_INFO ")");
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
