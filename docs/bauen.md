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
aus `lib_deps` geholt. Für die **pioarduino**-Variante (neuere ESP32-Cores) in
`platformio.ini` die `platform`-Zeile auf die pioarduino-URL umstellen (Kommentar
dort beachten). OTA-Upload: `upload_protocol`/`upload_port` in `platformio.ini`
aktivieren — oder bequemer das [Web-OTA](flashen.md#2-firmware-update-per-web-ota).

## Arduino-IDE
- Board: **ESP32 Dev Module**
- Libraries installieren: **PubSubClient** (Nick O'Leary), **ESPAsyncWebServer**
  und **AsyncTCP** (ESP32Async).
- Rest (WiFi, ArduinoOTA, Update, Preferences) kommt mit dem ESP32-Core.

Für ein OTA-taugliches Image: **Sketch → Kompilierte Binärdatei exportieren**, dann
die `.ino.bin` über `http://<IP>/update` hochladen.

## Versionierung
`FW_VERSION` in `config.h` ist ein SemVer-String (`"MAJOR.MINOR.PATCH"`). Bei jedem
Build erhöhen — dann lässt sich im Web-Footer prüfen, ob ein Flash/OTA wirklich
angekommen ist.
