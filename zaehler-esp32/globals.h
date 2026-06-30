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

// ─── Thread-Safety: Web-Handler -> loop() ─────────────────────────────────────
// Async-Handler laufen in einem EIGENEN Task. PubSubClient (MQTT) und die UARTs sind
// NICHT thread-safe -> Web-Handler setzen nur Werte/Flags, die heiklen Seiteneffekte
// führt loop() aus (mqtt.publish, applyStrom/applyMqtt, readHeat, Neustart nach OTA).
volatile bool reqRead          = false;   // /read  -> Wärme jetzt lesen
volatile bool applyStromPending = false;  // Strom-UART neu initialisieren
volatile bool applyMqttPending  = false;  // MQTT neu verbinden
volatile bool pubHeatCfg        = false;  // interval_h per MQTT publizieren
volatile bool pubStromCfg       = false;  // send_s per MQTT publizieren
unsigned long restartAt         = 0;      // geplanter Neustart nach Web-OTA (millis)

// kleine Helfer für GET-Query-Argumente eines Async-Requests
static bool   reqHas(AsyncWebServerRequest* r, const char* n) { return r->hasParam(n); }
static String reqArg(AsyncWebServerRequest* r, const char* n) { return r->hasParam(n) ? r->getParam(n)->value() : String(); }

char telegram[TELEGRAM_BUF];

// ─── Konfiguration (aus NVS geladen) ──────────────────────────────────────────
bool     heatEnabled   = true;
uint8_t  heatIntervalH = HEAT_INTERVAL_DEF_H;
uint8_t  heatTxPin     = HEAT_TX_DEF;
uint8_t  heatRxPin     = HEAT_RX_DEF;
bool     stromEnabled  = true;
uint8_t  stromRxPin    = STROM_RX_DEF;
uint16_t stromMqttS    = STROM_MQTT_DEF_S;   // MQTT-Sendeintervall Strom (Sekunden)
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

unsigned long lastHeat = 0, lastStromMqtt = 0;
bool otaActive = false;            // true während OTA-Upload -> Messung pausiert
