// ─────────────────────────────────────────────────────────────────────────────
//  globals.h — globale Objekte & Laufzeit-Zustand
//
//  Wird aus zaehler-esp32.ino NACH config.h und den Library-Includes (WiFi.h,
//  AsyncTCP.h, ESPAsyncWebServer.h, PubSubClient.h, Preferences.h) inkludiert.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

// ─── Hardware-/Netz-Objekte ───────────────────────────────────────────────────
HardwareSerial Heat(1);   // UART1 — Wärmezähler
HardwareSerial Sml(2);    // UART2 — Stromzähler
AsyncWebServer server(80);
WiFiClient     wifiClient;
PubSubClient   mqtt(wifiClient);
Preferences    prefs;
DNSServer      dnsServer;  // Captive-Portal-DNS (nur im Provisioning-AP aktiv)

// ─── WLAN-Provisioning-Zustand ────────────────────────────────────────────────
const IPAddress apIP(192, 168, 4, 1);     // feste SoftAP-IP (Detection-Redirects!)
bool     apMode      = false;              // true = Setup-Portal aktiv, Zähler ruhen
unsigned long apStartedAt = 0;             // millis() beim Portal-Start (Timeout)
String   wifiSsid = "";                    // aus NVS geladene WLAN-Zugangsdaten
String   wifiPass = "";

// ─── Thread-Safety: Web-Handler -> loop() ─────────────────────────────────────
// Async-Handler laufen in einem EIGENEN Task. PubSubClient (MQTT) und die UARTs sind
// NICHT thread-safe -> Web-Handler setzen nur Werte/Flags, die heiklen Seiteneffekte
// führt loop() aus (mqtt.publish, applyStrom/applyMqtt, readHeat, Neustart nach OTA).
volatile bool reqRead          = false;   // /read  -> Wärme jetzt lesen
volatile bool applyStromPending = false;  // Strom-UART neu initialisieren
volatile bool applyMqttPending  = false;  // MQTT neu verbinden
volatile bool applySendLedPending = false; // Sende-Diode neu parken
volatile bool pubHeatCfg        = false;  // interval_h per MQTT publizieren
volatile bool pubStromCfg       = false;  // send_s per MQTT publizieren
volatile bool credSaveReq       = false;  // neue WLAN-Daten gesetzt -> speichern + reboot
volatile bool wifiResetReq      = false;  // "WLAN vergessen" -> Creds löschen + reboot
volatile bool scanReq           = false;  // WLAN-Scan im Portal starten
String        pendingSsid, pendingPass;   // vom Web-Handler befüllt, loop() speichert
unsigned long restartAt         = 0;      // geplanter Neustart nach Web-OTA (millis)

// ─── Crash-Diagnose ───────────────────────────────────────────────────────────
// Beim Boot einmalig aus der coredump-Partition gelesene Panic-Summary als JSON
// (captureLastCrash() in setup()). Statisch bis zum nächsten Crash -> kein Flash-
// Read pro /api-Poll. "{\"present\":false}" = kein (gültiger) Dump vorhanden.
String lastCrashJson = "{\"present\":false}";

// kleine Helfer für GET-Query-Argumente eines Async-Requests
static bool   reqHas(AsyncWebServerRequest* r, const char* n) { return r->hasParam(n); }
static String reqArg(AsyncWebServerRequest* r, const char* n) { return r->hasParam(n) ? r->getParam(n)->value() : String(); }

char telegram[TELEGRAM_BUF];

// ─── Konfiguration (aus NVS geladen) ──────────────────────────────────────────
bool     heatEnabled   = true;
uint8_t  heatIntervalH = HEAT_INTERVAL_DEF_H;   // nur Teiler von 24 (siehe snapHeatInterval)
uint16_t heatStartMin  = HEAT_START_DEF_MIN;    // Startuhrzeit (Minuten seit Mitternacht)
uint8_t  heatTxPin     = HEAT_TX_DEF;
uint8_t  heatRxPin     = HEAT_RX_DEF;
bool     stromEnabled  = true;
uint8_t  stromRxPin    = STROM_RX_DEF;
uint16_t stromMqttS    = STROM_MQTT_DEF_S;   // MQTT-Sendeintervall Strom (Sekunden)
bool     sendledEnabled = SENDLED_EN_DEF;    // Sende-Diode des SML-Kopfes parken
uint8_t  sendledPin     = SENDLED_PIN_DEF;
bool     sendledLevel   = SENDLED_LEVEL_DEF; // true = HIGH haelt die Diode dunkel
int      reqIdx        = 0;                   // Index in HEAT_REQUESTS/HEAT_REQ_NAMES
// MQTT-Broker-Konfiguration (aus NVS)
bool     mqttEnabled = MQTT_ENABLED_DEF;       // MQTT global an/aus (Default aus)
String   mqttServer = MQTT_SERVER_DEF;
uint16_t mqttPort   = MQTT_PORT_DEF;
String   mqttUser   = "";                     // leer = anonym
String   mqttPass   = "";
String   mqttRoot   = MQTT_ROOT_DEF;          // Haupttopic, in Einstellungen editierbar

inline unsigned long heatIntervalMs() { return (unsigned long)heatIntervalH * 3600000UL; }
inline unsigned long stromMqttMs()    { return (unsigned long)stromMqttS * 1000UL; }

// ─── Wärme-Scheduler: feste Wanduhrzeiten (NTP), driftfrei ────────────────────
// Intervall auf den nächstgelegenen Teiler von 24 h einrasten (Web darf zwar nur
// gültige Werte schicken, aber alte NVS-Werte / krumme Eingaben abfangen).
inline uint8_t snapHeatInterval(int h) {
  uint8_t best = HEAT_DIVISORS[0]; int bd = 1000;
  for (uint8_t d : HEAT_DIVISORS) { int diff = abs((int)d - h); if (diff < bd) { bd = diff; best = d; } }
  return best;
}

// Echtzeit verfügbar (NTP synchron)?
inline bool timeValid() { return time(nullptr) > TIME_VALID_EPOCH; }

// Epoch-Sekunde des zuletzt fälligen Slots (<= jetzt); 0 falls Zeit noch nicht gültig.
// DURCHLAUFENDES Raster: alle Minuten, die ≡ Startuhrzeit modulo Intervall sind — über
// Tagesgrenzen hinweg. Da das Intervall ein Teiler von 24 h ist, sind es 24/Intervall
// Slots pro Tag, die sich jeden Tag zur GLEICHEN Uhrzeit wiederholen, lückenlos über
// Mitternacht (z.B. 18:45/2h -> …16:45,18:45,20:45,22:45,00:45,02:45…).
// dueMin = Minuten seit heutiger Mitternacht des jüngsten Slots (kann <0 = gestern);
// mktime() normalisiert das und rechnet DST-fest in Epoch um.
inline time_t heatDueSlot() {
  time_t now = time(nullptr);
  if (now <= TIME_VALID_EPOCH) return 0;
  struct tm lt; localtime_r(&now, &lt);
  int minOfDay  = lt.tm_hour * 60 + lt.tm_min;
  int intervalM = (int)heatIntervalH * 60;
  int r = ((minOfDay - (int)heatStartMin) % intervalM + intervalM) % intervalM;  // 0..intervalM-1
  struct tm s = lt; s.tm_hour = 0; s.tm_min = minOfDay - r; s.tm_sec = 0; s.tm_isdst = -1;
  return mktime(&s);
}

// Epoch-Sekunde des nächsten Slots (> jetzt). Slots sind gleichmäßig -> due + Intervall.
inline time_t heatNextSlot() {
  time_t d = heatDueSlot();
  return d ? d + (time_t)heatIntervalH * 3600L : 0;
}

// MQTT-Topic-Präfixe aus dem Haupttopic ableiten (inkl. abschließendem '/').
inline String heatPrefix()  { return mqttRoot + "/Heat/"; }    // <root>/Heat/...
inline String stromPrefix() { return mqttRoot + "/Power/"; }   // <root>/Power/...

// ─── Wärme-Werte (generisch je Code) ──────────────────────────────────────────
String   heatCode[HEAT_MAX], heatVal[HEAT_MAX], heatUnit[HEAT_MAX], heatRaw[HEAT_MAX];
int      heatCount  = 0;
String   heatStatus = "init";
String   heatIdent  = "";
int      heatLastLen = 0;
unsigned long heatReads = 0, heatOk = 0, heatLastAt = 0;

// ─── Strom-Werte: benannte Hauptwerte (Startseite + stabile MQTT-Topics) ──────
double   stromBezugWh   = NAN;   // OBIS 1.8.0 (Wh)
double   stromEinspWh   = NAN;   // OBIS 2.8.0
double   stromLeistungW = NAN;   // OBIS 16.7.0
bool     stromValid     = false;
String   stromStatus    = "init";
unsigned long stromLastOk = 0;
// Strom-Werte: generische Liste ALLER geparsten OBIS-Codes
String   stromCode[STROM_MAX], stromValStr[STROM_MAX], stromUnitStr[STROM_MAX];
int      stromCount = 0;

// ─── SML-Empfangspuffer ───────────────────────────────────────────────────────
uint8_t  smlBuf[SML_BUF];
int      smlLen = 0;
int      smlEndPos = 0;             // Index hinter dem '1A' des End-Escapes (0 = keins gesehen)

// ─── Strom-Plausibilität + SML-CRC-Diagnose ──────────────────────────────────
uint32_t stromMaxW = STROM_MAXW_DEF;            // |Leistung|-Grenze in W (0 = Prüfung aus)
unsigned long stromCrcOk = 0, stromCrcErr = 0;  // Telegramme: CRC gültig / verworfen
unsigned long stromImplaus = 0;                 // Leistungswerte über der Plausi-Grenze verworfen

unsigned long lastHeat = 0, lastStromMqtt = 0;
long lastHeatSlot = -1;             // Epoch des zuletzt gelesenen Slots (kantengesteuert)
bool otaActive = false;            // true während OTA-Upload -> Messung pausiert
