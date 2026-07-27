# Selber bauen

Nur nötig, wenn du den Code änderst — zum reinen Nutzen reichen die fertigen
[Release-Images](flashen.md).

## Projektstruktur
```
esp32-zaehler-reader/
├─ zaehler-esp32/
│  ├─ zaehler-esp32.ino     # Einstieg: Includes + setup() + loop()
│  ├─ config.h              # Konstanten & Defaults (Pins, Intervalle, FW-Version)
│  ├─ globals.h             # globale Objekte + Laufzeit-Zustand
│  ├─ net_mqtt.h            # WLAN- + MQTT-Verbindung + Setup-Portal
│  ├─ heat.h                # Wärmezähler (D0 / IEC 62056-21)
│  ├─ strom.h               # Stromzähler (SML-Parser)
│  ├─ web.h                 # JSON-API, Konfig-Handler, Routen, Web-OTA, Setup-Portal
│  └─ web_pages.h           # CSS + HTML-Seiten (PROGMEM), inkl. Setup-Portal
├─ platformio.ini
├─ README.md
└─ LICENSE
```

Der Sketch ist auf mehrere Header aufgeteilt, die alle als **eine**
Translation-Unit aus `zaehler-esp32.ino` in fester Reihenfolge inkludiert werden
(keine separate `.cpp`-Kompilierung nötig).

## PlatformIO (VSCode, empfohlen)
`platformio.ini` zeigt per `src_dir` auf den Sketch-Ordner — du musst nichts nach
`src/` kopieren.

1. PlatformIO-Projekt öffnen (Ordner `esp32-zaehler-reader/`).
2. Bauen/Flashen über die PlatformIO-Toolbar oder:
   ```bash
   pio run                 # kompilieren
   pio run -t upload       # per USB flashen
   pio device monitor      # serieller Monitor @115200
   ```

Die Libraries (`PubSubClient`, `ESPAsyncWebServer`, `AsyncTCP`) werden automatisch
aus `lib_deps` geholt.

**Plattform: seit FW 1.6.0 pioarduino** — Arduino-Core 3.3.9 / ESP-IDF 5.5.4, in
`platformio.ini` auf eine feste Release-URL gepinnt (reproduzierbare Builds). Nur
dieser Core bringt den ETH-Wrapper für den **W5500 über SPI**; der frühere
Standard-Core 2.0.17 (`espressif32@^6.9.0`) kennt ausschließlich RMII-PHYs und steht
als auskommentierter Rückfall daneben. **Nicht versehentlich zurückdrehen** — ohne
Core 3.x fällt LAN ersatzlos weg.

**Partitionsschema `min_spiffs`** (`board_build.partitions`): Core 3.x macht die Binary
deutlich größer. Mit dem Default-Layout (App-Slot 1,25 MB) wäre sie zu 99,7 % voll und
OTA damit praktisch tot; `min_spiffs` gibt ~1,9 MB je App-Slot und bleibt zweislot-fähig.
Das Gerät nutzt nur NVS, kein SPIFFS/LittleFS — die kleine Datenpartition stört nicht.

OTA-Upload: `upload_protocol`/`upload_port` in `platformio.ini`
aktivieren — oder bequemer das [Web-OTA](flashen.md#2-firmware-update-per-web-ota).

## Arduino-IDE
- Board: **ESP32 Dev Module** — passt für beide Boards (ESP32 Dev Kit C V4 wie
  MH-ET LIVE D1 mini ESP32).
- **ESP32-Core 3.x** im Boardverwalter wählen. Mit Core 2.x fehlt der ETH-Wrapper für
  den W5500, die Firmware lässt sich nicht übersetzen.
- **Partitionsschema: „Minimal SPIFFS"** (1,9 MB App / OTA) — mit dem Default-Schema
  passt die Firmware ab 1.6.0 nicht mehr sinnvoll, siehe oben.
- Libraries installieren: **PubSubClient** (Nick O'Leary), **ESPAsyncWebServer**
  und **AsyncTCP** (ESP32Async).
- Rest (WiFi, ETH, ArduinoOTA, Update, Preferences) kommt mit dem ESP32-Core.

Für ein OTA-taugliches Image: **Sketch → Kompilierte Binärdatei exportieren**, dann
die `.ino.bin` über `http://<IP>/update` hochladen.

## Versionierung
`FW_VERSION` in `config.h` ist ein SemVer-String (`"MAJOR.MINOR.PATCH"`). Bei jedem
Build erhöhen — dann lässt sich im Web-Footer prüfen, ob ein Flash/OTA wirklich
angekommen ist.
