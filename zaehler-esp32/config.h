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

// ─── Ethernet (W5500 über SPI) + Netz-Policy ──────────────────────────────────
// Das W5500-Modul hängt am freien VSPI-Vierer; als SPI-Host dient SPI2 (HSPI), die
// Pins werden per GPIO-Matrix dorthin geroutet — bei 20 MHz unkritisch. Belegt sind
// damit 18/19/23 (Bus) sowie 21 (CS), 34 (IRQ-Draht, s. u.) und 26 (Reset).
// WICHTIG: Diese sechs GPIOs stehen dadurch NICHT mehr für Zählerköpfe zur Verfügung
// und sind aus validInPin()/validOutPin() (web.h) sowie INPINS/OUTPINS (web_pages.h)
// entfernt. Modul-VCC an 5V ODER 3V3 — das Modul hat einen eigenen AMS1117-3.3;
// 5 V ist vorzuziehen, das entlastet den Regler des ESP-Boards. Signale 5-V-tolerant.
#define W5500_SCK_PIN   18
#define W5500_MISO_PIN  19
#define W5500_MOSI_PIN  23
#define W5500_CS_PIN    21
// IRQ bewusst AUS (-1): der Treiber pollt dann alle 10 ms (ETH_SPI_SUPPORTS_NO_IRQ,
// Arduino-Core 3.x) — bei unserer Datenmenge unkritisch. Grund: der als INT verdrahtete
// GPIO34 ist input-only und hat keinen internen Pull-up; ETH.begin() quittiert das mit
// "gpio_pullup_en(85): GPIO number error". Am 26.07.2026 im Feld verifiziert, dass LAN
// ohne IRQ trägt. Der Draht darf am Modul bleiben, deshalb bleibt 34 oben gesperrt.
#define W5500_IRQ_PIN   -1
#define W5500_RST_PIN   26
#define W5500_PHY_ADDR  1
// 8 statt 20 MHz: bei 20 MHz brach die SPI-Strecke am 26.07.2026 unter Volllast
// (Web-OTA, 1,3 MB) reproduzierbar zusammen — "emac_w5500_receive: write RX RD failed" /
// "w5500_send_command: read SCR failed", im Leerlauf dagegen fehlerfrei. Typisch für
// fliegende Verdrahtung ohne kurzen GND-Rückweg. 8 MHz reichen für 100 Mbit/s Ethernet
// bei unserer Datenmenge um Größenordnungen; erst bei sauberer Platine wieder erhöhen.
#define W5500_SPI_MHZ   8

// Netz-Policy, im Web umschaltbar (NVS "net_mode"). Ein Wechsel greift erst nach
// einem Neustart — Interfaces zur Laufzeit umzuschalten ist beim ESP32 fragil, und
// der Neustart kostet hier nur Sekunden.
#define NET_MODE_AUTO 0   // LAN bevorzugt, WLAN als Rückfall (Default)
#define NET_MODE_ETH  1   // nur LAN
#define NET_MODE_WIFI 2   // nur WLAN — der W5500 wird gar nicht erst initialisiert
#define NET_MODE_DEF  NET_MODE_AUTO
// Auto-Modus: so lange nach dem Start auf eine LAN-IP warten, bevor zusätzlich das
// WLAN hochgefahren wird. Link-Aushandlung + DHCP brauchen typisch 2–4 s; 8 s geben
// Reserve, ohne den Start spürbar zu verzögern (die Zähler laufen derweil weiter).
static const unsigned long ETH_WAIT_MS = 8000;
// Rettungsanker für den Modus "nur LAN": Kommt binnen dieser Zeit keine LAN-IP
// (Kabel ab, Switch tot, Modul defekt), öffnet der ESP das Setup-Portal. Ohne das
// wäre das Gerät nach einer Fehlkonfiguration nur noch per USB erreichbar.
static const unsigned long ETH_LOCKOUT_MS = 120000;   // 2 min

// ─── Selbstheilung: Watchdogs (gegen stummes Hängen ohne Reboot) ──────────────
// (A) Verbindungs-Watchdog: Ist im laufenden Betrieb länger als diese Zeit DURCHGEHEND
// KEIN Interface oben (weder LAN noch WLAN), holt ein bloßes WiFi.begin() den (oft
// verklemmten) WLAN-Treiber nicht zurück -> gezielter Neustart als Selbstheilung (wie
// der apMode-Timeout). Das verwandelt einen stummen Ausfall (>1 h beobachtet) in eine
// Lücke von Sekunden. Grund wird über den Reboot als /api "reboot_by" sichtbar gemacht.
// Seit 1.6.0 zählt "irgendein Interface up" — bei gestecktem Kabel deckt das LAN einen
// WLAN-Ausfall also ab, ohne dass der Watchdog anschlägt.
static const unsigned long NET_WATCHDOG_MS = 300000UL;      // 5 min ohne Netz -> Reboot
// (B) Task-Watchdog (TWDT): rebootet, wenn loop() länger als dies NICHT zurückkehrt
// (echte Einzel-Blockade, z.B. hängendes Serial.flush()); reset_reason wird task_wdt.
// Muss über der längsten legitimen loop()-Dauer liegen (WLAN-Connect-Busy-Wait 15 s
// + Wärme-Lesezyklus ~6-10 s). Der Core initialisiert den TWDT beim Boot auf 5 s;
// setup() konfiguriert ihn auf diesen Wert um. Zusätzlich füttern die bekannten
// Langläufer (ensureWifi/readHeat) den TWDT intern, damit ein evtl. fehlschlagendes
// Um-Init nicht zu Fehl-Reboots führt.
static const uint8_t TASK_WDT_TIMEOUT_S = 30;
// MQTT-Connect/Read hart begrenzen (PubSubClient-Default 15 s), damit ein Broker, der
// TCP annimmt aber beim Handshake stockt, die loop() nicht lange blockiert. Bewusst < 5 s:
// so bleibt loop() auch dann unter der Watchdog-Grenze, falls das TWDT-Um-Init auf 30 s
// nicht greifen sollte (dann gilt der Core-Default 5 s). Für einen LAN-Broker reichlich.
static const uint16_t MQTT_SOCKET_TIMEOUT_S = 4;

// ─── Firmware-Version ─────────────────────────────────────────────────────────
// FW_VERSION als SemVer-String "MAJOR.MINOR.PATCH" bei jedem neuen Build erhöhen.
// Der Build-Zeitstempel (__DATE__/__TIME__) aktualisiert sich automatisch beim
// Kompilieren und zeigt, ob ein Flash/OTA wirklich angekommen ist. Beides wird
// auf der Startseite gezeigt.
#define FW_VERSION  "1.6.1"
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
// Plausibilitäts-Obergrenze für die Momentan-Leistung (16.7.0) — Web-einstellbar (NVS).
// |Leistung| über dieser Grenze gilt als Telegramm-Ausreißer und wird verworfen (schützt
// DB/MQTT vor Werten wie dem 1-MW-Peak). 0 = Prüfung aus. Default 15 kW: ~2,3× des
// real gemessenen Haushalts-Peaks (6,57 kW, DB currentpowerhome 14 d; nie >8 kW),
// unter dem Hausanschluss-Limit (3×35 A ≈ 24 kW), weit unter Müllwerten.
#define STROM_MAXW_DEF  15000   // W
#define STROM_MAXW_MAX  100000  // Obergrenze der Einstellung (W)

// ─── Puffergrößen ─────────────────────────────────────────────────────────────
#define TELEGRAM_BUF 2600
#define SML_BUF      1024
#define HEAT_MAX     80      // UH50 liefert ~70 OBIS-Codes
#define STROM_MAX    32      // generisch geparste SML-OBIS-Werte
