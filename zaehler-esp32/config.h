// ─────────────────────────────────────────────────────────────────────────────
//  config.h — Kompilierzeit-Konstanten & Defaults
//
//  Wird aus zaehler-esp32.ino inkludiert. Enthält NUR #defines / const-Defaults;
//  veränderlicher Laufzeit-Zustand (aus NVS) liegt in globals.h.
//  Reihenfolge der Includes in der .ino beachten: config.h zuerst.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

// ─── MQTT-Defaults (Host/Port/User/PW zur Laufzeit über Web + NVS änderbar) ───
#define MQTT_SERVER_DEF  "192.168.179.55"   // ioBroker-Host
#define MQTT_PORT_DEF    1883
static const char* MQTT_CLIENT_ID = "esp32-zaehler";

// Haupt-/Root-Topic (in den Einstellungen editierbar, NVS). Darunter liegen die
// beiden Zweige Heat und Power:  <root>/Heat/data/6_8 , <root>/Power/data/16_7_0
#define MQTT_ROOT_DEF  "ESP32smartmeter"

static const char* HOSTNAME = "esp32-zaehler";             // OTA + Hostname

// ─── Firmware-Version ─────────────────────────────────────────────────────────
// FW_VERSION bei jedem neuen Build hochzählen. Der Build-Zeitstempel
// (__DATE__/__TIME__) aktualisiert sich automatisch beim Kompilieren und zeigt,
// ob ein Flash/OTA wirklich angekommen ist. Beides wird auf der Startseite gezeigt.
#define FW_VERSION  5
#define FW_BUILD    (__DATE__ " " __TIME__)

// ─── Wärmezähler (UART1) — Default-Pins (Web-änderbar) ────────────────────────
#define HEAT_TX_DEF   17     // -> Lesekopf Rx (300 Baud Anfrage)
#define HEAT_RX_DEF   16     // <- Lesekopf Tx (Datenbaud Antwort)
static const unsigned long HEAT_FIRST_BYTE_MS = 5000;   // Warten auf 1. Antwortbyte
static const unsigned long HEAT_IDLE_MS       = 1000;   // Telegramm-Ende = Stille

// Leseintervall Wärme: 1..24 h (Web-einstellbar, NVS)
#define HEAT_INTERVAL_MIN_H  1
#define HEAT_INTERVAL_MAX_H  24
#define HEAT_INTERVAL_DEF_H  1

// Umschaltbare Anfrage: 0 = "/?!\r\n" (IEC-Standard, funktioniert am UH50),
//                       1 = "/#!\r\n" (herstellerspezifischer Direktmodus, Fallback)
static const char* HEAT_REQUESTS[2]  = { "/?!\r\n", "/#!\r\n" };
static const char* HEAT_REQ_NAMES[2] = { "/?!", "/#!" };

// ─── Stromzähler (UART2, Hichi SML) — Default-Pin (Web-änderbar) ──────────────
#define STROM_RX_DEF  27     // <- Hichi Tx (Daten vom Lesekopf)
#define SML_INVERT    false  // manche Hichi-Köpfe invertieren -> ggf. true testen
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
