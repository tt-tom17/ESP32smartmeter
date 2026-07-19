// ─────────────────────────────────────────────────────────────────────────────
//  config.h — Kompilierzeit-Konstanten & Defaults
//
//  Wird aus zaehler-esp32.ino inkludiert. Enthält NUR #defines / const-Defaults;
//  veränderlicher Laufzeit-Zustand (aus NVS) liegt in globals.h.
//  Reihenfolge der Includes in der .ino beachten: config.h zuerst.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

// ─── MQTT-Defaults (alles zur Laufzeit über Web + NVS änderbar) ───────────────
// MQTT ist standardmäßig AUS — das Gerät läuft auch als reines Web-Display. Über
// die Einstellungen einschalten und Broker eintragen. Die Default-IP ist nur ein
// neutraler Platzhalter, der ohnehin per Weboberfläche überschrieben wird.
#define MQTT_ENABLED_DEF false
#define MQTT_SERVER_DEF  "192.168.1.10"
#define MQTT_PORT_DEF    1883
static const char* MQTT_CLIENT_ID = "esp32-zaehler";

// Haupt-/Root-Topic (in den Einstellungen editierbar, NVS). Darunter liegen die
// beiden Zweige Heat und Power:  <root>/Heat/data/6_8 , <root>/Power/data/16_7_0
#define MQTT_ROOT_DEF  "ESP32smartmeter"

static const char* HOSTNAME = "esp32-zaehler";             // OTA + Hostname

// ─── WLAN-Provisioning (SoftAP + Captive Portal) ──────────────────────────────
// Beim Erststart (keine WLAN-Daten im NVS) oder wenn die erste Verbindung nach
// dem Boot scheitert, öffnet der ESP ein OFFENES Setup-WLAN. Der User verbindet
// sich, trägt im Portal (192.168.4.1) SSID+Passwort ein -> NVS -> Reboot -> STA.
#define AP_SSID  "Zaehler-Setup"                            // offener Provisioning-AP
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000; // STA-Connect-Timeout
// Sind bereits WLAN-Daten gespeichert (Gerät lief schon), aber die Verbindung
// scheitert beim Boot (z.B. Router nach Stromausfall noch nicht oben): Portal nur
// vorübergehend öffnen, dann rebooten und STA neu versuchen -> selbstheilend.
// Ein FRISCHES Gerät (keine Creds) lässt das Portal dauerhaft offen.
static const unsigned long AP_PORTAL_TIMEOUT_MS = 90000;    // 90 s

// ─── Firmware-Version ─────────────────────────────────────────────────────────
// FW_VERSION als SemVer-String "MAJOR.MINOR.PATCH" bei jedem neuen Build erhöhen.
// Der Build-Zeitstempel (__DATE__/__TIME__) aktualisiert sich automatisch beim
// Kompilieren und zeigt, ob ein Flash/OTA wirklich angekommen ist. Beides wird
// auf der Startseite gezeigt.
#define FW_VERSION  "1.3.1"
#define FW_BUILD    (__DATE__ " " __TIME__)

// ─── Zeit / NTP ───────────────────────────────────────────────────────────────
// Für die Wärme-Abfrage zu festen Wanduhrzeiten braucht der ESP echte Zeit (er hat
// keine gepufferte RTC). NTP wird nach dem WLAN-Connect gestartet; TZ inkl. Sommer-/
// Winterzeit für Deutschland -> eingestellte Uhrzeiten bleiben über DST stabil.
#define TZ_INFO      "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER1  "pool.ntp.org"
#define NTP_SERVER2  "time.google.com"
// Zeit gilt als synchron, sobald die Uhr nach ~2020 steht (Epoch > 1.6e9).
static const time_t TIME_VALID_EPOCH = 1600000000UL;
// Ohne NTP-Sync (kein Internet) nach dieser Zeit auf millis()-Intervall zurückfallen.
static const unsigned long NTP_GRACE_MS = 20000;

// ─── Wärmezähler (UART1) — Default-Pins (Web-änderbar) ────────────────────────
#define HEAT_TX_DEF   17     // -> Lesekopf Rx (300 Baud Anfrage)
#define HEAT_RX_DEF   16     // <- Lesekopf Tx (Datenbaud Antwort)
static const unsigned long HEAT_FIRST_BYTE_MS = 5000;   // Warten auf 1. Antwortbyte
static const unsigned long HEAT_IDLE_MS       = 1000;   // Telegramm-Ende = Stille

// Leseintervall Wärme: NUR Teiler von 24 h (Web-einstellbar, NVS). Dadurch ergeben
// die Slots ab Startuhrzeit jeden Tag exakt dieselben Wanduhrzeiten OHNE Lücke über
// Mitternacht (z.B. 6 h ab 05:55 -> 05:55/11:55/17:55/23:55, dann wieder 05:55).
#define HEAT_INTERVAL_MIN_H  1
#define HEAT_INTERVAL_MAX_H  24
#define HEAT_INTERVAL_DEF_H  1
static const uint8_t HEAT_DIVISORS[] = { 1, 2, 3, 4, 6, 8, 12, 24 };  // 24 % d == 0

// Startuhrzeit der Wärme-Abfrage als Minuten seit Mitternacht (0..1439, NVS).
// Default 0 = Mitternacht -> mit 1 h Intervall wie bisher zur vollen Stunde.
#define HEAT_START_DEF_MIN  0

// Umschaltbare Anfrage: 0 = "/?!\r\n" (IEC-Standard, funktioniert am UH50),
//                       1 = "/#!\r\n" (herstellerspezifischer Direktmodus, Fallback)
static const char* HEAT_REQUESTS[2]  = { "/?!\r\n", "/#!\r\n" };
static const char* HEAT_REQ_NAMES[2] = { "/?!", "/#!" };

// ─── Stromzähler (UART2, Hichi SML) — Default-Pin (Web-änderbar) ──────────────
#define STROM_RX_DEF  27     // <- Hichi Tx (Daten vom Lesekopf)
#define SML_INVERT    false  // manche Hichi-Köpfe invertieren -> ggf. true testen
// Sende-Diode des SML-Kopfes parken (verhindert Einstreuung in den Lesesensor) — Web-änderbar
#define SENDLED_EN_DEF     1    // Sende-Diode standardmäßig aktiv parken
#define SENDLED_PIN_DEF    25   // Board-GPIO, an dem die Sende-Diode haengt
#define SENDLED_LEVEL_DEF  1    // 1 = HIGH haelt die Diode dunkel (dieser Hichi-Kopf); ggf. 0 = LOW
// Strom-Sendeintervall (MQTT) — Pendant zu Tasmota TelePeriod, Web-einstellbar (NVS).
// Der SML-Zähler sendet selbst 1-2x/s; schneller als ~1-2 s bringt keine neuen Werte.
#define STROM_MQTT_MIN_S  2
#define STROM_MQTT_MAX_S  300
#define STROM_MQTT_DEF_S  10
static const unsigned long STROM_STALE_MS = 30000;  // ohne Telegramm -> "stale"

// ─── Puffergrößen ─────────────────────────────────────────────────────────────
#define TELEGRAM_BUF 2600
#define SML_BUF      1024
#define HEAT_MAX     80      // UH50 liefert ~70 OBIS-Codes
#define STROM_MAX    32      // generisch geparste SML-OBIS-Werte
