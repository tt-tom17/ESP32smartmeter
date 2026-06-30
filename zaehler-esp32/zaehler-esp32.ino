/*
 * ESP32 (WROOM-32) – Wärme- + Stromzähler-Reader
 * ───────────────────────────────────────────────────────────────────────────
 *  Wärme : Landis+Gyr UH50 / T550   via D0 (IEC 62056-21), 300 TX / 2400 RX, 7E1
 *          Ablauf: 40x 0x00 Wake-up + Sign-on "/?!\r\n" @300 -> Identifikation
 *          lesen -> Baudrate aus 5. Zeichen -> (Mode C: ACK) -> Datenblock lesen.
 *  Strom : SML-Smart-Meter (eHZ)    via Hichi TTL-IR-Lesekopf, 9600 8N1, Push.
 *          Generischer Parser: liest ALLE OBIS-Werte des Telegramms.
 *  Web   : Mehrseitige, handytaugliche Oberfläche
 *            "/"        Startseite (Übersicht Strom + Wärme + Verbindung/MQTT)
 *            "/strom"   alle Stromwerte, Auslesen an/aus, Lesekopf-GPIO wählen
 *            "/waerme"  alle Wärmewerte, Auslesen an/aus, Intervall 1-24 h, GPIOs
 *            "/update"  Firmware-Upload (.ino.bin) im Browser
 *          Konfiguration (Intervall, an/aus, GPIOs) liegt im NVS (übersteht Reboot).
 *  MQTT  : alle Werte               -> ioBroker
 *
 *  UART-Belegung (ESP32 hat 3 HW-UARTs):
 *    UART0  USB-Debug / Log
 *    UART1  Wärmezähler  – re-begin 300<->Datenbaud (TX/RX laufen NACHEINANDER)
 *    UART2  Stromzähler  – 9600 8N1, Dauerempfang (großer RX-Puffer gegen Verlust)
 *
 *  Libraries (Arduino Library Manager):
 *    - PubSubClient (Nick O'Leary)
 *  Rest (WiFi, WebServer, ArduinoOTA, Update, Preferences) ist im ESP32-Core.
 *
 *  Board: "ESP32 Dev Module" , serieller Monitor @115200
 * ───────────────────────────────────────────────────────────────────────────
 */

#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

#include "secrets.h"     // WLAN-Zugangsdaten: WIFI_SSID / WIFI_PASS
                         // -> secrets.example.h nach secrets.h kopieren und ausfüllen
#include "web_pages.h"   // CSS + HTML-Seiten als PROGMEM (ausgelagert, s. Datei-Kommentar)

// ─── Konfiguration (Defaults; zur Laufzeit über Web + NVS änderbar) ───────────

// MQTT-Defaults (Host/Port/User/PW zur Laufzeit über Web + NVS änderbar)
#define MQTT_SERVER_DEF  "192.168.179.55"   // ioBroker-Host
#define MQTT_PORT_DEF    1883
const char* MQTT_CLIENT_ID = "esp32-zaehler";

const char* MQTT_HEAT_PREFIX  = "waermezaehler/";   // -> waermezaehler/data/6_8
const char* MQTT_STROM_PREFIX = "stromzaehler/";    // -> stromzaehler/data/16_7_0

const char* HOSTNAME       = "esp32-zaehler";       // OTA + Hostname

// ─── Firmware-Version ─────────────────────────────────────────────────────────
// FW_VERSION bei jedem neuen Build hochzählen. Der Build-Zeitstempel
// (__DATE__/__TIME__) aktualisiert sich automatisch beim Kompilieren und zeigt,
// ob ein Flash/OTA wirklich angekommen ist. Beides wird auf der Startseite gezeigt.
#define FW_VERSION  3
#define FW_BUILD    (__DATE__ " " __TIME__)

// Wärmezähler (UART1) — Default-Pins (Web-änderbar)
#define HEAT_TX_DEF   17     // -> Lesekopf Rx (300 Baud Anfrage)
#define HEAT_RX_DEF   16     // <- Lesekopf Tx (Datenbaud Antwort)
const unsigned long HEAT_FIRST_BYTE_MS = 5000;   // Warten auf 1. Antwortbyte
const unsigned long HEAT_IDLE_MS       = 1000;   // Telegramm-Ende = Stille

// Leseintervall Wärme: 1..24 h (Web-einstellbar, NVS)
#define HEAT_INTERVAL_MIN_H  1
#define HEAT_INTERVAL_MAX_H  24
#define HEAT_INTERVAL_DEF_H  1

// Umschaltbare Anfrage: 0 = "/?!\r\n" (IEC-Standard, funktioniert am UH50),
//                       1 = "/#!\r\n" (herstellerspezifischer Direktmodus, Fallback)
const char* HEAT_REQUESTS[2]  = { "/?!\r\n", "/#!\r\n" };
const char* HEAT_REQ_NAMES[2] = { "/?!", "/#!" };
int reqIdx = 0;

// Stromzähler (UART2, Hichi SML) — Default-Pin (Web-änderbar)
#define STROM_RX_DEF  27     // <- Hichi Tx (Daten vom Lesekopf)
#define SML_INVERT    false  // manche Hichi-Köpfe invertieren -> ggf. true testen
// Strom-Sendeintervall (MQTT) — Pendant zu Tasmota TelePeriod, Web-einstellbar (NVS).
// Der SML-Zähler sendet selbst 1-2x/s; schneller als ~1-2 s bringt keine neuen Werte.
#define STROM_MQTT_MIN_S  2
#define STROM_MQTT_MAX_S  300
#define STROM_MQTT_DEF_S  10
const unsigned long STROM_STALE_MS = 30000;  // ohne Telegramm -> "stale"

#define TELEGRAM_BUF 2600
#define SML_BUF      1024
#define HEAT_MAX     80      // UH50 liefert ~70 OBIS-Codes
#define STROM_MAX    32      // generisch geparste SML-OBIS-Werte

// ─── Interna ────────────────────────────────────────────────────────────────

HardwareSerial Heat(1);   // UART1
HardwareSerial Sml(2);    // UART2
WebServer      web(80);
WiFiClient     wifiClient;
PubSubClient   mqtt(wifiClient);
Preferences    prefs;

char telegram[TELEGRAM_BUF];

// Konfiguration (aus NVS geladen)
bool     heatEnabled  = true;
uint8_t  heatIntervalH = HEAT_INTERVAL_DEF_H;
uint8_t  heatTxPin    = HEAT_TX_DEF;
uint8_t  heatRxPin    = HEAT_RX_DEF;
bool     stromEnabled = true;
uint8_t  stromRxPin   = STROM_RX_DEF;
uint16_t stromMqttS   = STROM_MQTT_DEF_S;   // MQTT-Sendeintervall Strom (Sekunden)
// MQTT-Broker-Konfiguration (aus NVS)
String   mqttServer = MQTT_SERVER_DEF;
uint16_t mqttPort   = MQTT_PORT_DEF;
String   mqttUser   = "";                   // leer = anonym
String   mqttPass   = "";

unsigned long heatIntervalMs() { return (unsigned long)heatIntervalH * 3600000UL; }
unsigned long stromMqttMs()    { return (unsigned long)stromMqttS * 1000UL; }

// Wärme-Werte (generisch je Code)
String   heatCode[HEAT_MAX], heatVal[HEAT_MAX], heatUnit[HEAT_MAX], heatRaw[HEAT_MAX];
int      heatCount  = 0;
String   heatStatus = "init";
String   heatIdent  = "";
int      heatLastLen = 0;
unsigned long heatReads = 0, heatOk = 0, heatLastAt = 0;

// Strom-Werte: benannte Hauptwerte (für Startseite + stabile MQTT-Topics)
double   stromBezugWh   = NAN;   // OBIS 1.8.0 (Wh)
double   stromEinspWh   = NAN;   // OBIS 2.8.0
double   stromLeistungW = NAN;   // OBIS 16.7.0
bool     stromValid     = false;
String   stromStatus    = "init";
unsigned long stromLastOk = 0;
// Strom-Werte: generische Liste ALLER geparsten OBIS-Codes
String   stromCode[STROM_MAX], stromValStr[STROM_MAX], stromUnitStr[STROM_MAX];
int      stromCount = 0;

// SML-Empfangspuffer
uint8_t  smlBuf[SML_BUF];
int      smlLen = 0;

unsigned long lastHeat = 0, lastStromMqtt = 0;
bool otaActive = false;            // true während OTA-Upload -> Messung pausiert

// ─── WiFi / MQTT ─────────────────────────────────────────────────────────────

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
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;

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

  String lwt = String(MQTT_HEAT_PREFIX) + "online";
  bool ok;
  if (mqttUser.length())
    ok = mqtt.connect(MQTT_CLIENT_ID, mqttUser.c_str(), mqttPass.c_str(), lwt.c_str(), 0, true, "0");
  else
    ok = mqtt.connect(MQTT_CLIENT_ID, lwt.c_str(), 0, true, "0");   // anonym + LWT
  if (ok) mqtt.publish(lwt.c_str(), "1", true);
}

// MQTT-Server/Port aus aktueller Konfig setzen und Verbindung neu aufbauen
void applyMqtt() {
  mqtt.disconnect();
  mqtt.setServer(mqttServer.c_str(), mqttPort);   // ensureMqtt() verbindet neu
}

// '.' und '*' -> '_'  =>  "6.26*01" -> "6_26_01"  (MQTT-tauglich)
String topicify(const String& code) {
  String t;
  for (char c : code) t += (c == '.' || c == '*') ? '_' : c;
  return t;
}

// ─── Wärmezähler: D0 / IEC 62056-21 ──────────────────────────────────────────

int parseHeat(const char* buf) {
  heatCount = 0;
  const char* p = buf;
  while (*p && heatCount < HEAT_MAX) {
    const char* open  = strchr(p, '(');
    if (!open) break;
    const char* close = strchr(open, ')');
    if (!close) break;

    String code, inner;
    for (const char* q = p;        q < open;  q++) code  += *q;
    for (const char* q = open + 1; q < close; q++) inner += *q;
    code.trim();
    if (code.length() == 0) { p = close + 1; continue; }

    String value = inner, unit = "";
    int star = inner.indexOf('*');
    if (star >= 0 && inner.indexOf('&') < 0) {     // nur splitten, wenn eindeutig
      value = inner.substring(0, star);
      unit  = inner.substring(star + 1);
    }

    heatCode[heatCount] = code;
    heatVal[heatCount]  = value;
    heatUnit[heatCount] = unit;
    heatRaw[heatCount]  = (value != inner) ? inner : "";
    heatCount++;
    p = close + 1;
  }
  return heatCount;
}

void publishHeat() {
  for (int i = 0; i < heatCount; i++) {
    String base = String(MQTT_HEAT_PREFIX) + "data/" + topicify(heatCode[i]);
    mqtt.publish(base.c_str(), heatVal[i].c_str(), true);
    if (heatUnit[i].length()) mqtt.publish((base + "/unit").c_str(), heatUnit[i].c_str(), true);
    if (heatRaw[i].length())  mqtt.publish((base + "/raw").c_str(),  heatRaw[i].c_str(),  true);
  }
}

// Baudrate-Kennbuchstabe (IEC 62056-21, 5. Zeichen der Ident).
long baudFromChar(char z, bool* modeC) {
  switch (z) {
    case '0': *modeC = true; return 300;
    case '1': *modeC = true; return 600;
    case '2': *modeC = true; return 1200;
    case '3': *modeC = true; return 2400;
    case '4': *modeC = true; return 4800;
    case '5': *modeC = true; return 9600;
    case '6': *modeC = true; return 19200;
    case 'A': *modeC = false; return 600;
    case 'B': *modeC = false; return 1200;
    case 'C': *modeC = false; return 2400;
    case 'D': *modeC = false; return 4800;
    case 'E': *modeC = false; return 9600;
    case 'F': *modeC = false; return 19200;
    default:  *modeC = false; return 300;     // Mode A: bei 300 bleiben
  }
}

void readHeat() {
  heatReads++;
  heatLastAt = millis();
  String stTopic = String(MQTT_HEAT_PREFIX) + "status";

  // Phase 1: @300 Baud 7E1 — Wake-up (40x 0x00) + Anfrage senden
  Heat.begin(300, SERIAL_7E1, heatRxPin, heatTxPin);
  delay(20);
  while (Heat.available()) Heat.read();
  for (int i = 0; i < 40; i++) Heat.write((uint8_t)0x00);
  Heat.print(HEAT_REQUESTS[reqIdx]);
  Heat.flush();

  // Phase 2: Identifikation @300 lesen ( /MMMZident\r\n )
  char ident[80]; int idn = 0;
  unsigned long dl = millis() + 2000;
  while (millis() < dl && idn < 79) {
    if (Heat.available()) {
      char c = (char)Heat.read();
      dl = millis() + 400;
      if (c == '\r') continue;
      if (c == '\n') break;
      ident[idn++] = c;
    }
  }
  ident[idn] = '\0';
  heatIdent = String(ident);

  if (idn < 5 || ident[0] != '/') {
    heatStatus = "keine/ungueltige Identifikation \"" + heatIdent + "\" (Anfrage " + HEAT_REQ_NAMES[reqIdx] + ")";
    heatCount = 0; heatLastLen = 0;
    mqtt.publish(stTopic.c_str(), "no_response", true);
    Serial.printf("[D0] Ident ungueltig/leer: \"%s\"\n", ident);
    return;
  }

  bool modeC = false;
  long dataBaud = baudFromChar(ident[4], &modeC);
  Serial.printf("[D0] Ident=\"%s\"  '%c' -> %ld Baud, Mode %s\n",
                ident, ident[4], dataBaud, modeC ? "C" : "B/A");

  // Phase 3: Mode C -> ACK ( 0x06 '0' Z '0' \r\n )
  if (modeC) {
    Heat.write((uint8_t)0x06); Heat.write('0');
    Heat.write((uint8_t)ident[4]); Heat.write('0');
    Heat.write('\r'); Heat.write('\n');
    Heat.flush();
  }

  // Phase 4: Daten-Baudrate 7E1, Datenblock bis '!' lesen
  Heat.begin(dataBaud, SERIAL_7E1, heatRxPin, heatTxPin);
  int len = 0;
  unsigned long deadline = millis() + HEAT_FIRST_BYTE_MS;
  while (millis() < deadline && len < TELEGRAM_BUF - 1) {
    if (Heat.available()) {
      char c = (char)Heat.read();
      deadline = millis() + HEAT_IDLE_MS;
      if (c == 0x02 || c == 0x03 || c == '\r' || c == '\n') continue;
      if (c == '!') break;
      telegram[len++] = c;
    }
  }
  telegram[len] = '\0';
  heatLastLen = len;

  if (len == 0) {
    heatStatus = "Ident ok (" + heatIdent + "), aber kein Datenblock @" + String(dataBaud);
    heatCount = 0;
    mqtt.publish(stTopic.c_str(), "ident_only", true);
    return;
  }

  int n = parseHeat(telegram);
  heatOk++;
  heatStatus = "ok / " + String(n) + " Codes / " + String(len) + " B @" + String(dataBaud);
  mqtt.publish(stTopic.c_str(), heatStatus.c_str(), true);
  publishHeat();
  Serial.printf("[D0] OK: %d Codes geparst, publiziert.\n", n);
}

// ─── Stromzähler: SML-Parser ─────────────────────────────────────────────────

// Ein TLV-Element überspringen (rekursiv für Listen). Liefert Zeiger danach.
const uint8_t* smlSkip(const uint8_t* p, const uint8_t* end) {
  if (p >= end) return end;
  uint8_t tl = *p;
  if (tl & 0x80) return end;
  uint8_t type = tl & 0x70;
  uint8_t len  = tl & 0x0F;
  if (type == 0x70) {                   // Liste mit 'len' Elementen
    const uint8_t* q = p + 1;
    for (int i = 0; i < len && q < end; i++) q = smlSkip(q, end);
    return q;
  }
  if (len == 0) return p + 1;           // 0x00 (Füll-/Endbyte)
  return p + len;
}

// Ein TLV-Element als (vorzeichenbehaftete) Ganzzahl lesen.
long smlInt(const uint8_t* p, const uint8_t* end, bool* ok) {
  *ok = false;
  if (p >= end) return 0;
  uint8_t tl = *p;
  if (tl & 0x80) return 0;
  uint8_t type = tl & 0x70;
  int n = (tl & 0x0F) - 1;
  if (n <= 0 || p + 1 + n > end) return 0;
  bool neg = (type == 0x50) && (p[1] & 0x80);
  long v = neg ? -1L : 0L;
  for (int i = 0; i < n; i++) v = (v << 8) | p[1 + i];
  *ok = true;
  return v;
}

// DLMS-Einheitencode -> Klartext (häufige Stromzähler-Einheiten)
const char* dlmsUnit(uint8_t u) {
  switch (u) {
    case 27: return "W";   case 28: return "VA";  case 29: return "var";
    case 30: return "Wh";  case 31: return "VAh"; case 32: return "varh";
    case 33: return "A";   case 35: return "V";   case 44: return "Hz";
    case 4:  return "°";   case 8:  return "°C";
    default: return "";
  }
}

int smlFindObis(const uint8_t* buf, int len, const uint8_t* obis) {
  for (int i = 0; i + 7 <= len; i++)
    if (buf[i] == 0x07 && memcmp(buf + i + 1, obis, 6) == 0) return i;
  return -1;
}

double smlValue(const uint8_t* buf, int len, const uint8_t* obis) {
  int idx = smlFindObis(buf, len, obis);
  if (idx < 0) return NAN;
  const uint8_t* end = buf + len;
  const uint8_t* p   = buf + idx + 7;
  p = smlSkip(p, end);                 // status
  p = smlSkip(p, end);                 // valTime
  p = smlSkip(p, end);                 // unit
  bool ok;
  long scaler = smlInt(p, end, &ok);
  if (!ok) scaler = 0;
  p = smlSkip(p, end);                 // -> value
  long value = smlInt(p, end, &ok);
  if (!ok) return NAN;
  return (double)value * pow(10.0, (double)scaler);
}

// Generisch ALLE OBIS-Werte (Medium Strom, obis[0]==1) aus dem Telegramm holen.
void smlScanAll(const uint8_t* buf, int len) {
  stromCount = 0;
  const uint8_t* end = buf + len;
  for (int i = 0; i + 7 <= len && stromCount < STROM_MAX; i++) {
    if (buf[i] != 0x07) continue;
    const uint8_t* obis = buf + i + 1;
    if (obis[0] != 0x01) continue;            // nur Strom-Register

    const uint8_t* p = buf + i + 7;
    p = smlSkip(p, end);                       // status
    p = smlSkip(p, end);                       // valTime
    uint8_t unitCode = 0;                      // unit (0x62 = unsigned 1 Byte)
    if (p < end) { uint8_t tl = *p; if ((tl & 0x70) == 0x60 && (tl & 0x0F) >= 2) unitCode = p[1]; }
    p = smlSkip(p, end);                       // unit
    bool ok;
    long scaler = smlInt(p, end, &ok);
    if (!ok) scaler = 0;
    p = smlSkip(p, end);                       // -> value
    uint8_t vt = (p < end) ? (*p & 0x70) : 0;  // nur Integer-Werte (kein octet-string)
    if (vt != 0x50 && vt != 0x60) continue;
    long value = smlInt(p, end, &ok);
    if (!ok) continue;

    String code = String(obis[2]) + "." + String(obis[3]) + "." + String(obis[4]);
    bool dup = false;
    for (int k = 0; k < stromCount; k++) if (stromCode[k] == code) { dup = true; break; }
    if (dup) continue;

    double v = (double)value * pow(10.0, (double)scaler);
    char vb[24]; dtostrf(v, 0, (scaler < 0 ? -scaler : 0), vb);
    stromCode[stromCount]    = code;
    stromValStr[stromCount]  = String(vb);
    stromUnitStr[stromCount] = dlmsUnit(unitCode);
    stromCount++;
  }
}

void smlProcess(const uint8_t* buf, int len) {
  static const uint8_t OBIS_BEZUG[6] = {0x01,0x00,0x01,0x08,0x00,0xFF}; // 1.8.0
  static const uint8_t OBIS_EINSP[6] = {0x01,0x00,0x02,0x08,0x00,0xFF}; // 2.8.0
  static const uint8_t OBIS_LEIST[6] = {0x01,0x00,0x10,0x07,0x00,0xFF}; // 16.7.0

  double b = smlValue(buf, len, OBIS_BEZUG);
  double e = smlValue(buf, len, OBIS_EINSP);
  double w = smlValue(buf, len, OBIS_LEIST);

  bool any = false;
  if (!isnan(b)) { stromBezugWh   = b; any = true; }
  if (!isnan(e)) { stromEinspWh   = e; any = true; }
  if (!isnan(w)) { stromLeistungW = w; any = true; }

  smlScanAll(buf, len);                       // komplette Werteliste

  if (any || stromCount > 0) { stromValid = true; stromLastOk = millis(); }
}

void smlPoll() {
  while (Sml.available()) {
    uint8_t b = (uint8_t)Sml.read();
    if (smlLen < SML_BUF) smlBuf[smlLen++] = b; else smlLen = 0;
    if (smlLen >= 8 &&
        memcmp(&smlBuf[smlLen - 8], "\x1B\x1B\x1B\x1B\x01\x01\x01\x01", 8) == 0) {
      memmove(smlBuf, &smlBuf[smlLen - 8], 8);
      smlLen = 8;
    }
    if (smlLen >= 5 &&
        memcmp(&smlBuf[smlLen - 5], "\x1B\x1B\x1B\x1B\x1A", 5) == 0) {
      smlProcess(smlBuf, smlLen - 5);
      smlLen = 0;
    }
  }
  if (stromValid && millis() - stromLastOk > STROM_STALE_MS) stromStatus = "stale";
  else if (stromValid)                                       stromStatus = "ok";
}

void publishStrom() {
  if (!stromEnabled || !stromValid) return;
  char v[24];
  if (!isnan(stromBezugWh)) {
    dtostrf(stromBezugWh / 1000.0, 0, 3, v);
    mqtt.publish((String(MQTT_STROM_PREFIX) + "bezug_kwh").c_str(), v, true);
  }
  if (!isnan(stromEinspWh)) {
    dtostrf(stromEinspWh / 1000.0, 0, 3, v);
    mqtt.publish((String(MQTT_STROM_PREFIX) + "einspeisung_kwh").c_str(), v, true);
  }
  if (!isnan(stromLeistungW)) {
    dtostrf(stromLeistungW, 0, 1, v);
    mqtt.publish((String(MQTT_STROM_PREFIX) + "leistung_w").c_str(), v, true);
  }
  for (int i = 0; i < stromCount; i++) {       // komplette Liste generisch
    String t = String(MQTT_STROM_PREFIX) + "data/" + topicify(stromCode[i]);
    mqtt.publish(t.c_str(), stromValStr[i].c_str(), true);
    if (stromUnitStr[i].length()) mqtt.publish((t + "/unit").c_str(), stromUnitStr[i].c_str(), true);
  }
  mqtt.publish((String(MQTT_STROM_PREFIX) + "status").c_str(), stromStatus.c_str(), true);
}

// Stromzähler-UART nach Konfig (de)aktivieren / Pin wechseln
void applyStrom() {
  Sml.end();
  smlLen = 0;
  if (stromEnabled) {
    Sml.setRxBufferSize(4096);
    Sml.begin(9600, SERIAL_8N1, stromRxPin, -1, SML_INVERT);
    stromStatus = "init";
    Serial.printf("[CFG] Strom: AN, GPIO%u\n", stromRxPin);
  } else {
    stromValid = false;
    stromStatus = "aus";
    Serial.println("[CFG] Strom: AUS");
  }
}

// ─── Webseiten: CSS + HTML sind in web_pages.h ausgelagert (PROGMEM) ──────────
// (verhindert den .ino-Prototyp-Generator-Bug mit "async function" in Raw-Strings)

String jsonEscape(const String& s) {
  String o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o;
}

void handleApi() {
  unsigned long nextS = 0;
  if (heatEnabled && lastHeat != 0) {
    unsigned long el = millis() - lastHeat;
    nextS = (el >= heatIntervalMs()) ? 0 : (heatIntervalMs() - el) / 1000;
  }

  String j = "{";
  j += "\"uptime_s\":" + String(millis() / 1000);
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"mqtt\":" + String(mqtt.connected() ? "true" : "false");
  j += ",\"mqtt_host\":\"" + jsonEscape(mqttServer) + "\"";
  j += ",\"mqtt_port\":" + String(mqttPort);
  j += ",\"mqtt_user\":\"" + jsonEscape(mqttUser) + "\"";
  j += ",\"mqtt_haspw\":" + String(mqttPass.length() ? "true" : "false");
  j += ",\"fw_ver\":" + String(FW_VERSION);
  j += ",\"fw_build\":\"" + jsonEscape(FW_BUILD) + "\"";

  // Strom
  j += ",\"strom\":{";
  j += "\"enabled\":" + String(stromEnabled ? "true" : "false");
  j += ",\"gpio\":" + String(stromRxPin);
  j += ",\"send_s\":" + String(stromMqttS);
  j += ",\"status\":\"" + jsonEscape(stromStatus) + "\"";
  if (!isnan(stromBezugWh))   j += ",\"bezug_kwh\":" + String(stromBezugWh / 1000.0, 3);
  if (!isnan(stromEinspWh))   j += ",\"einspeisung_kwh\":" + String(stromEinspWh / 1000.0, 3);
  if (!isnan(stromLeistungW)) j += ",\"leistung_w\":" + String(stromLeistungW, 1);
  j += ",\"codes\":[";
  for (int i = 0; i < stromCount; i++) {
    if (i) j += ",";
    j += "{\"code\":\"" + jsonEscape(stromCode[i]) + "\",";
    j += "\"value\":\"" + jsonEscape(stromValStr[i]) + "\",";
    j += "\"unit\":\"" + jsonEscape(stromUnitStr[i]) + "\"}";
  }
  j += "]}";

  // Wärme
  j += ",\"heat\":{";
  j += "\"enabled\":" + String(heatEnabled ? "true" : "false");
  j += ",\"interval_h\":" + String(heatIntervalH);
  j += ",\"next_read_s\":" + String(nextS);
  j += ",\"tx\":" + String(heatTxPin);
  j += ",\"rx\":" + String(heatRxPin);
  j += ",\"request\":\"" + String(HEAT_REQ_NAMES[reqIdx]) + "\"";
  j += ",\"ident\":\"" + jsonEscape(heatIdent) + "\"";
  j += ",\"reads\":" + String(heatReads);
  j += ",\"ok\":" + String(heatOk);
  j += ",\"last_len\":" + String(heatLastLen);
  j += ",\"status\":\"" + jsonEscape(heatStatus) + "\"";
  j += ",\"codes\":[";
  for (int i = 0; i < heatCount; i++) {
    if (i) j += ",";
    j += "{\"code\":\"" + jsonEscape(heatCode[i]) + "\",";
    j += "\"value\":\"" + jsonEscape(heatVal[i]) + "\",";
    j += "\"unit\":\"" + jsonEscape(heatUnit[i]) + "\",";
    j += "\"raw\":\"" + jsonEscape(heatRaw[i]) + "\"}";
  }
  j += "]}}";
  web.send(200, "application/json", j);
}

bool validGpio(int g) { return g >= 0 && g <= 39; }

void handleSetHeat() {
  if (web.hasArg("en")) {
    heatEnabled = web.arg("en").toInt() != 0;
    prefs.putUChar("heat_en", heatEnabled ? 1 : 0);
  }
  if (web.hasArg("h")) {
    int h = web.arg("h").toInt();
    if (h < HEAT_INTERVAL_MIN_H) h = HEAT_INTERVAL_MIN_H;
    if (h > HEAT_INTERVAL_MAX_H) h = HEAT_INTERVAL_MAX_H;
    heatIntervalH = (uint8_t)h;
    prefs.putUChar("heat_h", heatIntervalH);
    mqtt.publish((String(MQTT_HEAT_PREFIX) + "interval_h").c_str(), String(heatIntervalH).c_str(), true);
  }
  if (web.hasArg("tx")) { int g = web.arg("tx").toInt(); if (validGpio(g)) { heatTxPin = g; prefs.putUChar("heat_tx", g); } }
  if (web.hasArg("rx")) { int g = web.arg("rx").toInt(); if (validGpio(g)) { heatRxPin = g; prefs.putUChar("heat_rx", g); } }
  Serial.printf("[CFG] Wärme: %s, %u h, TX=GPIO%u RX=GPIO%u\n",
                heatEnabled ? "AN" : "AUS", heatIntervalH, heatTxPin, heatRxPin);
  web.send(200, "text/plain", "ok");
}

void handleSetStrom() {
  bool changed = false;
  if (web.hasArg("en")) {
    stromEnabled = web.arg("en").toInt() != 0;
    prefs.putUChar("strom_en", stromEnabled ? 1 : 0);
    changed = true;
  }
  if (web.hasArg("rx")) {
    int g = web.arg("rx").toInt();
    if (validGpio(g)) { stromRxPin = g; prefs.putUChar("strom_rx", g); changed = true; }
  }
  if (web.hasArg("s")) {                       // MQTT-Sendeintervall (s)
    int s = web.arg("s").toInt();
    if (s < STROM_MQTT_MIN_S) s = STROM_MQTT_MIN_S;
    if (s > STROM_MQTT_MAX_S) s = STROM_MQTT_MAX_S;
    stromMqttS = (uint16_t)s;
    prefs.putUShort("strom_s", stromMqttS);
    mqtt.publish((String(MQTT_STROM_PREFIX) + "send_s").c_str(), String(stromMqttS).c_str(), true);
  }
  if (changed) applyStrom();
  web.send(200, "text/plain", "ok");
}

// MQTT-Broker konfigurieren: host, port, user, pw (alle optional). Leeres pw-Feld
// lässt das Passwort UNVERÄNDERT (sonst würde jedes Speichern es löschen).
void handleSetMqtt() {
  if (web.hasArg("host")) { mqttServer = web.arg("host"); prefs.putString("mqtt_host", mqttServer); }
  if (web.hasArg("port")) {
    int p = web.arg("port").toInt();
    if (p > 0 && p <= 65535) { mqttPort = (uint16_t)p; prefs.putUShort("mqtt_port", mqttPort); }
  }
  if (web.hasArg("user")) { mqttUser = web.arg("user"); prefs.putString("mqtt_user", mqttUser); }
  if (web.hasArg("pw")) {
    String pw = web.arg("pw");
    if (pw.length()) { mqttPass = pw; prefs.putString("mqtt_pass", mqttPass); }
  }
  Serial.printf("[CFG] MQTT %s:%u user=%s\n",
                mqttServer.c_str(), mqttPort, mqttUser.length() ? mqttUser.c_str() : "(anonym)");
  applyMqtt();
  web.send(200, "text/plain", "ok");
}

void setupWebOta() {
  web.on("/update", HTTP_GET, []() {
    web.send_P(200, "text/html", UPDATE_PAGE);
  });
  web.on("/update", HTTP_POST,
    []() {
      web.send(200, "text/plain", Update.hasError() ? "FEHLER beim Flashen" : "OK - Neustart...");
      delay(500);
      ESP.restart();
    },
    []() {
      HTTPUpload& up = web.upload();
      if (up.status == UPLOAD_FILE_START) {
        otaActive = true;
        Serial.printf("[WebOTA] Start: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
      } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[WebOTA] OK: %u Bytes\n", up.totalSize);
        else Update.printError(Serial);
      }
    }
  );
}

// ─── Setup / Loop ────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32 Zähler-Reader startet...");

  // Konfiguration aus NVS laden
  prefs.begin("zaehler", false);
  heatEnabled   = prefs.getUChar("heat_en", 1) != 0;
  heatIntervalH = prefs.getUChar("heat_h", HEAT_INTERVAL_DEF_H);
  if (heatIntervalH < HEAT_INTERVAL_MIN_H) heatIntervalH = HEAT_INTERVAL_MIN_H;
  if (heatIntervalH > HEAT_INTERVAL_MAX_H) heatIntervalH = HEAT_INTERVAL_MAX_H;
  heatTxPin     = prefs.getUChar("heat_tx", HEAT_TX_DEF);
  heatRxPin     = prefs.getUChar("heat_rx", HEAT_RX_DEF);
  stromEnabled  = prefs.getUChar("strom_en", 1) != 0;
  stromRxPin    = prefs.getUChar("strom_rx", STROM_RX_DEF);
  stromMqttS    = prefs.getUShort("strom_s", STROM_MQTT_DEF_S);
  if (stromMqttS < STROM_MQTT_MIN_S) stromMqttS = STROM_MQTT_MIN_S;
  if (stromMqttS > STROM_MQTT_MAX_S) stromMqttS = STROM_MQTT_MAX_S;
  mqttServer = prefs.getString("mqtt_host", MQTT_SERVER_DEF);
  mqttPort   = prefs.getUShort("mqtt_port", MQTT_PORT_DEF);
  mqttUser   = prefs.getString("mqtt_user", "");
  mqttPass   = prefs.getString("mqtt_pass", "");
  Serial.printf("[CFG] Wärme %s %uh TX%u RX%u | Strom %s GPIO%u | MQTT %s:%u user=%s\n",
                heatEnabled ? "AN" : "AUS", heatIntervalH, heatTxPin, heatRxPin,
                stromEnabled ? "AN" : "AUS", stromRxPin,
                mqttServer.c_str(), mqttPort, mqttUser.length() ? mqttUser.c_str() : "(anonym)");

  applyStrom();                      // Strom-UART je nach Konfig starten

  ensureWifi();

  mqtt.setServer(mqttServer.c_str(), mqttPort);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(60);

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]() { otaActive = true;  Serial.println("[OTA] Update startet - Messung pausiert."); });
  ArduinoOTA.onError([](ota_error_t e) { otaActive = false; });
  ArduinoOTA.begin();

  web.on("/",          [](){ web.send_P(200, "text/html", MAIN_PAGE); });
  web.on("/strom",     [](){ web.send_P(200, "text/html", STROM_PAGE); });
  web.on("/waerme",    [](){ web.send_P(200, "text/html", WAERME_PAGE); });
  web.on("/style.css", [](){ web.sendHeader("Cache-Control", "max-age=86400"); web.send_P(200, "text/css", CSS); });
  web.on("/api",       handleApi);
  web.on("/setheat",   handleSetHeat);
  web.on("/setstrom",  handleSetStrom);
  web.on("/setmqtt",   handleSetMqtt);
  web.on("/read",      [](){ readHeat(); lastHeat = millis(); web.send(200, "text/plain", "ok"); });
  web.on("/toggle",    [](){ reqIdx = 1 - reqIdx; web.send(200, "text/plain", HEAT_REQ_NAMES[reqIdx]); });
  setupWebOta();
  web.begin();

  Serial.println("Setup fertig.");
}

void loop() {
  ArduinoOTA.handle();
  if (otaActive) return;
  ensureWifi();
  ensureMqtt();
  mqtt.loop();
  web.handleClient();

  if (stromEnabled) smlPoll();

  unsigned long now = millis();

  if (now - lastStromMqtt >= stromMqttMs()) {
    lastStromMqtt = now;
    publishStrom();
  }

  if (heatEnabled && (lastHeat == 0 || now - lastHeat >= heatIntervalMs())) {
    lastHeat = now;
    readHeat();
  }
}
