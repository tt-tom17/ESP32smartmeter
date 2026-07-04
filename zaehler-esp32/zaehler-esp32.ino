/*
 * ESP32 (WROOM-32) – Wärme- + Stromzähler-Reader
 * ───────────────────────────────────────────────────────────────────────────
 *  Wärme : Landis+Gyr UH50 / T550   via D0 (IEC 62056-21), 300 TX / 2400 RX, 7E1
 *          Ablauf: 40x 0x00 Wake-up + Sign-on "/?!\r\n" @300 -> Identifikation
 *          lesen -> Baudrate aus 5. Zeichen -> (Mode C: ACK) -> Datenblock lesen.
 *  Strom : SML-Smart-Meter (eHZ)    via Hichi TTL-IR-Lesekopf, 9600 8N1, Push.
 *          Generischer Parser: liest ALLE OBIS-Werte des Telegramms.
 *  Web   : Mehrseitige, handytaugliche Oberfläche
 *            "/"           Startseite (Übersicht Strom + Wärme + Verbindung/MQTT)
 *            "/strom"      alle Stromwerte
 *            "/waerme"     alle Wärmewerte + "Jetzt lesen"
 *            "/update"     Einstellungen (Strom/Wärme/MQTT) + Firmware-Upload
 *          Konfiguration (Intervall, an/aus, GPIOs, MQTT) liegt im NVS.
 *  MQTT  : alle Werte               -> ioBroker
 *
 *  UART-Belegung (ESP32 hat 3 HW-UARTs):
 *    UART0  USB-Debug / Log
 *    UART1  Wärmezähler  – re-begin 300<->Datenbaud (TX/RX laufen NACHEINANDER)
 *    UART2  Stromzähler  – 9600 8N1, Dauerempfang (großer RX-Puffer gegen Verlust)
 *
 *  Quelldatei-Aufteilung (alle .h werden in dieser .ino als EINE Translation-Unit
 *  inkludiert; Reihenfolge ist wichtig):
 *    config.h     Kompilierzeit-Konstanten & Defaults
 *    globals.h    globale Objekte + Laufzeit-Zustand (Heat/Sml/server/mqtt/prefs)
 *    net_mqtt.h   WLAN- + MQTT-Verbindung (+ topicify)
 *    heat.h       Wärmezähler D0/IEC 62056-21
 *    strom.h      Stromzähler SML-Parser
 *    web_pages.h  CSS + HTML-Seiten (PROGMEM)
 *    web.h        JSON-API, Web-Konfig-Handler, Routen, Web-OTA
 *
 *  Libraries: PubSubClient, ESP32Async/AsyncTCP, ESP32Async/ESPAsyncWebServer.
 *  Rest (WiFi, ArduinoOTA, Update, Preferences) ist im ESP32-Core.
 *  Board: "ESP32 Dev Module" , serieller Monitor @115200
 * ───────────────────────────────────────────────────────────────────────────
 */

#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>
#include <time.h>

// WLAN-Zugangsdaten kommen aus dem NVS und werden per Setup-Portal (offener SoftAP
// "Zaehler-Setup") eingerichtet — keine secrets.h mehr nötig. Siehe net_mqtt.h.
#include "config.h"      // Konstanten / Defaults
#include "globals.h"     // globale Objekte + Laufzeit-Zustand
#include "net_mqtt.h"    // WLAN + MQTT
#include "heat.h"        // Wärmezähler
#include "strom.h"       // Stromzähler
#include "web_pages.h"   // CSS + HTML-Seiten (PROGMEM)
#include "web.h"         // JSON-API + Web-Handler + Routen + OTA

// ─── Setup / Loop ────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32 Zähler-Reader startet...");

  // Konfiguration aus NVS laden
  prefs.begin("zaehler", false);
  loadWifiCreds();                   // WLAN-Zugangsdaten (leer -> Setup-Portal)
  heatEnabled   = prefs.getUChar("heat_en", 1) != 0;
  heatIntervalH = snapHeatInterval(prefs.getUChar("heat_h", HEAT_INTERVAL_DEF_H));
  heatStartMin  = prefs.getUShort("heat_start", HEAT_START_DEF_MIN);
  if (heatStartMin > 1439) heatStartMin = HEAT_START_DEF_MIN;
  heatTxPin     = prefs.getUChar("heat_tx", HEAT_TX_DEF);
  heatRxPin     = prefs.getUChar("heat_rx", HEAT_RX_DEF);
  stromEnabled  = prefs.getUChar("strom_en", 1) != 0;
  stromRxPin    = prefs.getUChar("strom_rx", STROM_RX_DEF);
  stromMqttS    = prefs.getUShort("strom_s", STROM_MQTT_DEF_S);
  if (stromMqttS < STROM_MQTT_MIN_S) stromMqttS = STROM_MQTT_MIN_S;
  if (stromMqttS > STROM_MQTT_MAX_S) stromMqttS = STROM_MQTT_MAX_S;
  mqttEnabled = prefs.getUChar("mqtt_en", MQTT_ENABLED_DEF ? 1 : 0) != 0;
  mqttServer = prefs.getString("mqtt_host", MQTT_SERVER_DEF);
  mqttPort   = prefs.getUShort("mqtt_port", MQTT_PORT_DEF);
  mqttUser   = prefs.getString("mqtt_user", "");
  mqttPass   = prefs.getString("mqtt_pass", "");
  mqttRoot   = prefs.getString("mqtt_root", MQTT_ROOT_DEF);
  Serial.printf("[CFG] WLAN '%s' | Wärme %s ab %02u:%02u alle %uh TX%u RX%u | Strom %s GPIO%u | MQTT %s:%u user=%s\n",
                wifiSsid.length() ? wifiSsid.c_str() : "(unkonfiguriert -> Portal)",
                heatEnabled ? "AN" : "AUS", heatStartMin / 60, heatStartMin % 60,
                heatIntervalH, heatTxPin, heatRxPin,
                stromEnabled ? "AN" : "AUS", stromRxPin,
                mqttServer.c_str(), mqttPort, mqttUser.length() ? mqttUser.c_str() : "(anonym)");

  applyStrom();                      // Strom-UART je nach Konfig starten

  ensureWifi();
  startTime();                       // NTP-Sync für feste Wärme-Abfragezeiten starten

  mqtt.setServer(mqttServer.c_str(), mqttPort);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(60);

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]() { otaActive = true;  Serial.println("[OTA] Update startet - Messung pausiert."); });
  ArduinoOTA.onError([](ota_error_t e) { otaActive = false; });
  ArduinoOTA.begin();

  setupWeb();                        // Routen registrieren + Server starten

  Serial.println("Setup fertig.");
}

void loop() {
  if (restartAt && millis() >= restartAt) { restartAt = 0; ESP.restart(); }
  ArduinoOTA.handle();
  if (otaActive) return;
  ensureWifi();                      // im apMode bedient das nur den Captive-DNS

  // WLAN-Provisioning: heikle Seiteneffekte gehören in loop(), nicht in Web-Handler
  if (credSaveReq)  { credSaveReq  = false; saveWifiCreds(pendingSsid, pendingPass); restartAt = millis() + 500; }
  if (wifiResetReq) { wifiResetReq = false; clearWifiCreds();                        restartAt = millis() + 500; }

  if (apMode) {                      // im Setup-Portal ruhen Zähler + MQTT
    if (scanReq) { scanReq = false; if (WiFi.scanComplete() == -2) WiFi.scanNetworks(true); }
    // Selbstheilung: Gerät MIT gespeicherten Creds nach Timeout neu starten und
    // STA erneut versuchen (Router nach Stromausfall inzwischen oben). Frisches
    // Gerät (keine Creds) bleibt zum Einrichten dauerhaft im Portal.
    if (!wifiSsid.isEmpty() && millis() - apStartedAt > AP_PORTAL_TIMEOUT_MS) restartAt = millis();
    return;
  }

  ensureMqtt();
  mqtt.loop();

  // Vom Async-Webserver angeforderte, NICHT thread-safe Aktionen hier ausführen:
  if (applyStromPending) { applyStromPending = false; applyStrom(); }
  if (applyMqttPending)  { applyMqttPending  = false; applyMqtt(); }
  if (pubHeatCfg)  { pubHeatCfg  = false; mqtt.publish((heatPrefix()  + "interval_h").c_str(), String(heatIntervalH).c_str(), true); }
  if (pubStromCfg) { pubStromCfg = false; mqtt.publish((stromPrefix() + "send_s").c_str(),     String(stromMqttS).c_str(),    true); }
  if (reqRead)     { reqRead     = false; readHeat(); lastHeat = millis(); }

  if (stromEnabled) smlPoll();

  unsigned long now = millis();

  if (now - lastStromMqtt >= stromMqttMs()) {
    lastStromMqtt = now;
    publishStrom();
  }

  // Wärme-Abfrage zu festen Wanduhrzeiten (NTP). Kantengesteuert: der jeweils fällige
  // Slot wird an seiner Epoch-Sekunde erkannt und genau EINMAL gelesen -> kein Drift
  // durch Lesedauer/Timeouts, kein Doppel-Feuern, und ein verpasster Slot (Gerät war
  // blockiert) wird beim nächsten freien loop() nachgeholt.
  if (heatEnabled) {
    if (timeValid()) {
      time_t slot = heatDueSlot();
      if (slot != 0 && (long)slot != lastHeatSlot) {
        lastHeatSlot = (long)slot;
        lastHeat = now;
        readHeat();
      }
    } else if (millis() > NTP_GRACE_MS && (lastHeat == 0 || now - lastHeat >= heatIntervalMs())) {
      // Fallback ohne NTP (kein Internet): wie früher das millis()-Intervall.
      lastHeat = now;
      readHeat();
    }
  }
}
